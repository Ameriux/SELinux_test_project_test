#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <selinux/selinux.h>
#include <selinux/context.h>

// 配置
#define SOCKET_PATH "/tmp/immutable_service.sock"
#define DATA_DIR "/Users/amireuxjoe/SELinux/SELinux_test_project_test/data"
#define LOG_FILE "/Users/amireuxjoe/SELinux/SELinux_test_project_test/data/service.log"
#define MAX_PATH_LEN 1024
#define MAX_DATA_SIZE (10 * 1024 * 1024) // 10MB
#define AUTH_TOKEN "test_token_immutable_123"  // 实际应用中应更安全
#define MIN_RETENTION_HOURS 24  // 文件保留最少24小时

// 命令类型
typedef enum {
    CMD_MODIFY = 1,        // 修改文件
    CMD_DELETE = 2,        // 删除文件
    CMD_RSYNC_UPDATE = 3,  // 增量更新
    CMD_GET_INFO = 4       // 获取文件信息
} command_type;

// 请求头
typedef struct {
    command_type cmd;
    char path[MAX_PATH_LEN];
    char token[128];
    size_t data_len;
    time_t timestamp;
} request_header;

// 文件元数据
typedef struct {
    time_t creation_time;
    time_t modification_time;
    char checksum[64];
} file_metadata;

// 全局变量
int server_fd = -1;
FILE *log_fp = NULL;

// 日志函数
void log_message(const char *level, const char *message, ...) {
    time_t now;
    struct tm *tm_info;
    char timestamp[64];
    va_list args;
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (log_fp) {
        fprintf(log_fp, "[%s] [%s] ", timestamp, level);
        va_start(args, message);
        vfprintf(log_fp, message, args);
        va_end(args);
        fprintf(log_fp, "\n");
        fflush(log_fp);
    }
    
    va_start(args, message);
    vsyslog(LOG_NOTICE, message, args);
    va_end(args);
}

// 处理终止信号
void handle_signal(int sig) {
    log_message("INFO", "收到信号 %d，关闭服务", sig);
    if (server_fd != -1) {
        close(server_fd);
        unlink(SOCKET_PATH);
    }
    if (log_fp) fclose(log_fp);
    closelog();
    exit(0);
}

// 设置SELinux上下文
int set_immutable_context(const char *path) {
    security_context_t current_context = NULL;
    security_context_t new_context = NULL;
    context_t context;
    int ret = -1;
    
    if (getfilecon(path, &current_context) < 0) {
        log_message("ERROR", "无法获取文件 %s 的上下文: %s", path, strerror(errno));
        return -1;
    }
    
    context = context_new(current_context);
    if (!context) {
        log_message("ERROR", "无法创建上下文: %s", strerror(errno));
        freecon(current_context);
        return -1;
    }
    
    // 设置type为immutable_file_t
    context_type_set(context, "immutable_file_t");
    new_context = context_str(context);
    
    // 应用新上下文
    ret = setfilecon(path, new_context);
    if (ret < 0) {
        log_message("ERROR", "无法设置文件 %s 的上下文: %s", path, strerror(errno));
    } else {
        log_message("INFO", "已设置文件 %s 的SELinux上下文为 %s", path, new_context);
    }
    
    context_free(context);
    freecon(current_context);
    return ret;
}

// 获取完整路径
char* get_full_path(const char *relative_path) {
    static char full_path[MAX_PATH_LEN];
    snprintf(full_path, MAX_PATH_LEN, "%s/%s", DATA_DIR, relative_path);
    return full_path;
}

// 计算文件checksum (简化版)
void calculate_checksum(const char *path, char *checksum, size_t checksum_size) {
    snprintf(checksum, checksum_size, "checksum_%ld", time(NULL));
    // 实际应用中应使用SHA256等哈希算法
}

// 保存文件元数据
int save_metadata(const char *path, file_metadata *metadata) {
    char meta_path[MAX_PATH_LEN];
    FILE *fp;
    
    snprintf(meta_path, MAX_PATH_LEN, "%s.meta", path);
    fp = fopen(meta_path, "w");
    if (!fp) {
        log_message("ERROR", "无法创建元数据文件: %s", meta_path);
        return -1;
    }
    
    fprintf(fp, "creation_time=%ld\n", metadata->creation_time);
    fprintf(fp, "modification_time=%ld\n", metadata->modification_time);
    fprintf(fp, "checksum=%s\n", metadata->checksum);
    
    fclose(fp);
    
    // 设置元数据文件的SELinux上下文
    set_immutable_context(meta_path);
    return 0;
}

// 加载文件元数据
int load_metadata(const char *path, file_metadata *metadata) {
    char meta_path[MAX_PATH_LEN];
    FILE *fp;
    char line[256];
    
    snprintf(meta_path, MAX_PATH_LEN, "%s.meta", path);
    fp = fopen(meta_path, "r");
    if (!fp) {
        // 文件不存在，初始化默认元数据
        metadata->creation_time = time(NULL);
        metadata->modification_time = time(NULL);
        strcpy(metadata->checksum, "initial");
        return -1;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        char *key = strtok(line, "=");
        char *value = strtok(NULL, "\n");
        
        if (key && value) {
            if (strcmp(key, "creation_time") == 0) {
                metadata->creation_time = atol(value);
            } else if (strcmp(key, "modification_time") == 0) {
                metadata->modification_time = atol(value);
            } else if (strcmp(key, "checksum") == 0) {
                strncpy(metadata->checksum, value, sizeof(metadata->checksum)-1);
            }
        }
    }
    
    fclose(fp);
    return 0;
}

// 验证请求
int authenticate_request(request_header *req) {
    // 令牌验证
    if (strcmp(req->token, AUTH_TOKEN) != 0) {
        log_message("WARNING", "认证失败: 无效令牌");
        return 0;
    }
    
    // 路径验证
    if (strlen(req->path) == 0 || strlen(req->path) >= MAX_PATH_LEN) {
        log_message("WARNING", "认证失败: 无效路径");
        return 0;
    }
    
    // 时间戳验证 (防止重放攻击)
    time_t now = time(NULL);
    if (labs(now - req->timestamp) > 300) { // 5分钟内有效
        log_message("WARNING", "认证失败: 过期请求");
        return 0;
    }
    
    return 1;
}

// 检查文件能否删除 (基于保留期)
int can_delete_file(const char *path) {
    file_metadata metadata;
    time_t now = time(NULL);
    
    if (load_metadata(path, &metadata) != 0) {
        log_message("WARNING", "无法加载元数据, 禁止删除: %s", path);
        return 0;
    }
    
    // 检查是否满足最小保留期
    double hours_since_creation = difftime(now, metadata.creation_time) / 3600.0;
    
    if (hours_since_creation < MIN_RETENTION_HOURS) {
        log_message("WARNING", "文件 %s 未达到最短保留期 (%.1f/%.1f 小时)", 
                   path, hours_since_creation, (double)MIN_RETENTION_HOURS);
        return 0;
    }
    
    return 1;
}

// 修改文件
int modify_file(const char *path, const char *data, size_t data_len) {
    FILE *fp;
    file_metadata metadata;
    
    // 加载现有元数据
    load_metadata(path, &metadata);
    
    // 打开并写入文件
    fp = fopen(path, "w");
    if (!fp) {
        log_message("ERROR", "无法打开文件进行写入: %s", path);
        return -1;
    }
    
    size_t written = fwrite(data, 1, data_len, fp);
    fclose(fp);
    
    if (written != data_len) {
        log_message("ERROR", "写入文件时出错: %s", path);
        return -1;
    }
    
    // 更新元数据
    metadata.modification_time = time(NULL);
    calculate_checksum(path, metadata.checksum, sizeof(metadata.checksum));
    
    // 保存元数据
    if (save_metadata(path, &metadata) != 0) {
        log_message("WARNING", "无法保存元数据: %s", path);
    }
    
    // 设置SELinux上下文
    set_immutable_context(path);
    
    log_message("INFO", "已成功修改文件: %s", path);
    return 0;
}

// 删除文件
int delete_file(const char *path) {
    // 检查是否满足删除条件
    if (!can_delete_file(path)) {
        log_message("WARNING", "删除被拒绝: 文件未达到保留期: %s", path);
        return -1;
    }
    
    // 删除文件和元数据
    char meta_path[MAX_PATH_LEN];
    snprintf(meta_path, MAX_PATH_LEN, "%s.meta", path);
    
    if (unlink(path) == -1) {
        log_message("ERROR", "无法删除文件: %s", path);
        return -1;
    }
    
    unlink(meta_path); // 忽略元数据删除失败
    
    log_message("INFO", "已成功删除文件: %s", path);
    return 0;
}

// 使用rsync进行增量更新
int rsync_update(const char *path, const char *source_data, size_t data_len) {
    char temp_path[MAX_PATH_LEN];
    char command[MAX_PATH_LEN * 2];
    FILE *fp;
    int result;
    
    // 创建临时源文件
    snprintf(temp_path, MAX_PATH_LEN, "%s.source", path);
    fp = fopen(temp_path, "w");
    if (!fp) {
        log_message("ERROR", "无法创建临时源文件: %s", temp_path);
        return -1;
    }
    
    fwrite(source_data, 1, data_len, fp);
    fclose(fp);
    
    // 如果目标不存在，则先创建
    if (access(path, F_OK) != 0) {
        fp = fopen(path, "w");
        if (fp) fclose(fp);
    }
    
    // 构建rsync命令
    snprintf(command, sizeof(command), "rsync -a --inplace %s %s", temp_path, path);
    
    log_message("INFO", "执行增量更新: %s", command);
    result = system(command);
    
    // 删除临时文件
    unlink(temp_path);
    
    if (result != 0) {
        log_message("ERROR", "rsync更新失败: %s", path);
        return -1;
    }
    
    // 更新元数据
    file_metadata metadata;
    load_metadata(path, &metadata);
    metadata.modification_time = time(NULL);
    calculate_checksum(path, metadata.checksum, sizeof(metadata.checksum));
    save_metadata(path, &metadata);
    
    // 设置SELinux上下文
    set_immutable_context(path);
    
    log_message("INFO", "已成功增量更新文件: %s", path);
    return 0;
}

// 获取文件信息
int get_file_info(const char *path, char *info_buffer, size_t buffer_size) {
    struct stat st;
    file_metadata metadata;
    
    if (stat(path, &st) != 0) {
        snprintf(info_buffer, buffer_size, "文件不存在");
        return -1;
    }
    
    load_metadata(path, &metadata);
    
    snprintf(info_buffer, buffer_size,
            "文件: %s\n"
            "大小: %ld 字节\n"
            "创建时间: %s"
            "修改时间: %s"
            "保留期满: %s\n"
            "校验和: %s\n",
            path, st.st_size,
            ctime(&metadata.creation_time),
            ctime(&metadata.modification_time),
            can_delete_file(path) ? "是" : "否",
            metadata.checksum);
    
    return 0;
}

int main() {
    struct sockaddr_un server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
    request_header req;
    void *data_buffer = NULL;
    char *full_path = NULL;
    
    // 初始化日志
    openlog("immutable_service", LOG_PID, LOG_DAEMON);
    log_fp = fopen(LOG_FILE, "a");
    log_message("INFO", "不可变文件管理服务启动");
    
    // 创建数据目录
    mkdir(DATA_DIR, 0755);
    
    // 设置信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    // 创建socket
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        log_message("ERROR", "无法创建socket: %s", strerror(errno));
        return 1;
    }
    
    // 准备地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);
    
    // 删除可能存在的旧socket文件
    unlink(SOCKET_PATH);
    
    // 绑定地址
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_message("ERROR", "无法绑定socket: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    // 设置socket权限
    chmod(SOCKET_PATH, 0666);
    
    // 监听连接
    if (listen(server_fd, 5) == -1) {
        log_message("ERROR", "无法监听socket: %s", strerror(errno));
        close(server_fd);
        unlink(SOCKET_PATH);
        return 1;
    }
    
    log_message("INFO", "等待连接在 %s", SOCKET_PATH);
    
    // 主循环
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            log_message("ERROR", "接受连接失败: %s", strerror(errno));
            continue;
        }
        
        log_message("INFO", "接受新连接");
        
        // 接收请求头
        if (recv(client_fd, &req, sizeof(req), 0) != sizeof(req)) {
            log_message("ERROR", "接收请求失败");
            close(client_fd);
            continue;
        }
        
        // 验证请求
        if (!authenticate_request(&req)) {
            const char *msg = "认证失败";
            send(client_fd, msg, strlen(msg), 0);
            close(client_fd);
            continue;
        }
        
        // 获取完整路径
        full_path = get_full_path(req.path);
        log_message("INFO", "处理命令: %d, 路径: %s", req.cmd, full_path);
        
        int result = -1;
        char info_buffer[4096] = {0};
        
        // 处理命令
        switch(req.cmd) {
            case CMD_MODIFY:
                if (req.data_len > 0 && req.data_len < MAX_DATA_SIZE) {
                    data_buffer = malloc(req.data_len);
                    if (data_buffer) {
                        if (recv(client_fd, data_buffer, req.data_len, 0) == req.data_len) {
                            result = modify_file(full_path, data_buffer, req.data_len);
                        }
                        free(data_buffer);
                    }
                }
                break;
                
            case CMD_DELETE:
                result = delete_file(full_path);
                break;
                
            case CMD_RSYNC_UPDATE:
                if (req.data_len > 0 && req.data_len < MAX_DATA_SIZE) {
                    data_buffer = malloc(req.data_len);
                    if (data_buffer) {
                        if (recv(client_fd, data_buffer, req.data_len, 0) == req.data_len) {
                            result = rsync_update(full_path, data_buffer, req.data_len);
                        }
                        free(data_buffer);
                    }
                }
                break;
                
            case CMD_GET_INFO:
                result = get_file_info(full_path, info_buffer, sizeof(info_buffer));
                break;
                
            default:
                log_message("WARNING", "未知命令: %d", req.cmd);
                break;
        }
        
        // 回传结果
        const char *msg;
        if (req.cmd == CMD_GET_INFO) {
            // 发送文件信息
            send(client_fd, info_buffer, strlen(info_buffer), 0);
        } else {
            // 发送操作结果
            msg = (result == 0) ? "操作成功" : "操作失败";
            send(client_fd, msg, strlen(msg), 0);
        }
        
        close(client_fd);
    }
    
    // 清理
    close(server_fd);
    unlink(SOCKET_PATH);
    if (log_fp) fclose(log_fp);
    closelog();
    
    return 0;
} 