#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <errno.h>

#include "immutable_client.h"

#define SOCKET_PATH "/tmp/immutable_service.sock"
#define MAX_PATH_LEN 1024
#define MAX_RESPONSE_SIZE 4096
#define AUTH_TOKEN "test_token_immutable_123"  // 需与服务端一致

// 连接到服务
static int connect_to_service() {
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("无法创建socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("无法连接到服务");
        close(sock_fd);
        return -1;
    }
    
    return sock_fd;
}

// 准备请求头
static void prepare_request(request_header *req, command_type cmd, const char *path, size_t data_len) {
    memset(req, 0, sizeof(request_header));
    req->cmd = cmd;
    strncpy(req->path, path, MAX_PATH_LEN - 1);
    strncpy(req->token, AUTH_TOKEN, sizeof(req->token) - 1);
    req->data_len = data_len;
    req->timestamp = time(NULL);
}

// 发送请求并接收响应
static int send_request_receive_response(int sock_fd, request_header *req, 
                                      const void *data, char *response, size_t response_size) {
    // 发送请求头
    if (send(sock_fd, req, sizeof(request_header), 0) != sizeof(request_header)) {
        perror("发送请求失败");
        return -1;
    }
    
    // 发送数据(如果有)
    if (data && req->data_len > 0) {
        if (send(sock_fd, data, req->data_len, 0) != req->data_len) {
            perror("发送数据失败");
            return -1;
        }
    }
    
    // 接收响应
    ssize_t recv_len = recv(sock_fd, response, response_size - 1, 0);
    if (recv_len <= 0) {
        perror("接收响应失败");
        return -1;
    }
    
    response[recv_len] = '\0';
    return recv_len;
}

// 修改不可变文件
int modify_immutable_file(const char *path, const char *data, size_t data_len) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求
    request_header req;
    prepare_request(&req, CMD_MODIFY, path, data_len);
    
    // 发送请求并接收响应
    char response[MAX_RESPONSE_SIZE];
    int result = send_request_receive_response(sock_fd, &req, data, response, sizeof(response));
    
    close(sock_fd);
    
    if (result > 0) {
        printf("服务响应: %s\n", response);
        return strstr(response, "成功") ? 0 : -1;
    }
    
    return -1;
}

// 删除不可变文件
int delete_immutable_file(const char *path) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求
    request_header req;
    prepare_request(&req, CMD_DELETE, path, 0);
    
    // 发送请求并接收响应
    char response[MAX_RESPONSE_SIZE];
    int result = send_request_receive_response(sock_fd, &req, NULL, response, sizeof(response));
    
    close(sock_fd);
    
    if (result > 0) {
        printf("服务响应: %s\n", response);
        return strstr(response, "成功") ? 0 : -1;
    }
    
    return -1;
}

// 增量更新文件
int rsync_update_immutable_file(const char *path, const char *data, size_t data_len) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return -1;
    }
    
    // 准备请求
    request_header req;
    prepare_request(&req, CMD_RSYNC_UPDATE, path, data_len);
    
    // 发送请求并接收响应
    char response[MAX_RESPONSE_SIZE];
    int result = send_request_receive_response(sock_fd, &req, data, response, sizeof(response));
    
    close(sock_fd);
    
    if (result > 0) {
        printf("服务响应: %s\n", response);
        return strstr(response, "成功") ? 0 : -1;
    }
    
    return -1;
}

// 获取文件信息
char* get_immutable_file_info(const char *path) {
    int sock_fd = connect_to_service();
    if (sock_fd == -1) {
        return NULL;
    }
    
    // 准备请求
    request_header req;
    prepare_request(&req, CMD_GET_INFO, path, 0);
    
    // 分配响应缓冲区
    char *info = malloc(MAX_RESPONSE_SIZE);
    if (!info) {
        close(sock_fd);
        return NULL;
    }
    
    // 发送请求并接收响应
    int result = send_request_receive_response(sock_fd, &req, NULL, info, MAX_RESPONSE_SIZE);
    
    close(sock_fd);
    
    if (result > 0) {
        return info;
    }
    
    free(info);
    return NULL;
}

// 主程序(用于命令行测试)
#ifdef CLIENT_MAIN
void print_usage(const char *prog_name) {
    printf("用法: %s <命令> <文件路径> [内容]\n", prog_name);
    printf("命令:\n");
    printf("  modify    - 修改文件\n");
    printf("  delete    - 删除文件\n");
    printf("  update    - 增量更新文件\n");
    printf("  info      - 获取文件信息\n");
    printf("示例:\n");
    printf("  %s modify test.txt \"这是测试内容\"\n", prog_name);
    printf("  %s delete test.txt\n", prog_name);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *cmd = argv[1];
    const char *path = argv[2];
    int result = -1;
    
    if (strcmp(cmd, "modify") == 0) {
        if (argc < 4) {
            printf("错误: modify命令需要提供文件内容\n");
            return 1;
        }
        const char *content = argv[3];
        result = modify_immutable_file(path, content, strlen(content));
    } 
    else if (strcmp(cmd, "delete") == 0) {
        result = delete_immutable_file(path);
    } 
    else if (strcmp(cmd, "update") == 0) {
        if (argc < 4) {
            printf("错误: update命令需要提供文件内容\n");
            return 1;
        }
        const char *content = argv[3];
        result = rsync_update_immutable_file(path, content, strlen(content));
    } 
    else if (strcmp(cmd, "info") == 0) {
        char *info = get_immutable_file_info(path);
        if (info) {
            printf("%s\n", info);
            free(info);
            result = 0;
        } else {
            printf("获取文件信息失败\n");
            result = -1;
        }
    } 
    else {
        printf("未知命令: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
    
    return (result == 0) ? 0 : 1;
}
#endif 