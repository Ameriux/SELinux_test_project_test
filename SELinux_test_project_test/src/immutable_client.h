#ifndef IMMUTABLE_CLIENT_H
#define IMMUTABLE_CLIENT_H

#include <stddef.h>
#include <time.h>

// 命令类型
typedef enum {
    CMD_MODIFY = 1,        // 修改文件
    CMD_DELETE = 2,        // 删除文件
    CMD_RSYNC_UPDATE = 3,  // 增量更新
    CMD_GET_INFO = 4       // 获取文件信息
} command_type;

// 请求头结构体
typedef struct {
    command_type cmd;
    char path[1024];
    char token[128];
    size_t data_len;
    time_t timestamp;
} request_header;

/**
 * 修改不可变文件
 * 
 * @param path 文件路径 (相对于数据目录)
 * @param data 文件内容
 * @param data_len 内容长度
 * @return 成功返回0，失败返回-1
 */
int modify_immutable_file(const char *path, const char *data, size_t data_len);

/**
 * 删除不可变文件
 * 
 * @param path 文件路径 (相对于数据目录)
 * @return 成功返回0，失败返回-1
 */
int delete_immutable_file(const char *path);

/**
 * 使用增量更新方式修改文件 (rsync)
 * 
 * @param path 文件路径 (相对于数据目录)
 * @param data 文件内容
 * @param data_len 内容长度
 * @return 成功返回0，失败返回-1
 */
int rsync_update_immutable_file(const char *path, const char *data, size_t data_len);

/**
 * 获取文件信息
 * 
 * @param path 文件路径 (相对于数据目录)
 * @return 成功返回包含文件信息的字符串，失败返回NULL (注意：调用者负责释放返回的内存)
 */
char* get_immutable_file_info(const char *path);

#endif /* IMMUTABLE_CLIENT_H */ 