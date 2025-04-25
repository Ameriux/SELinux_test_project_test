#!/bin/bash

BASE_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd $BASE_DIR

echo "===== SELinux 不可变文件安全测试 ====="

# 确保服务正在运行
if ! pgrep -f "immutable_service" > /dev/null; then
    echo "启动服务..."
    ./immutable_service &
    sleep 2
fi

# 创建测试文件
echo "1. 创建测试文件..."
./immutable_client modify test_security.txt "这是一个安全测试文件"
echo

# 测试直接访问文件
echo "2. 测试直接修改文件 (应该被记录但允许在permissive模式下)..."
echo "尝试直接写入:" > data/test_security.txt
cat data/test_security.txt
echo

# 测试时间限制删除
echo "3. 测试时间限制删除 (应该失败，因为未达到保留期)..."
./immutable_client delete test_security.txt
echo

# 测试错误的认证令牌
echo "4. 测试错误的认证令牌 (需手动查看日志)..."
# 这里我们使用错误的令牌发送请求
# 由于我们不能修改客户端代码中的AUTH_TOKEN，所以只能提示
echo "注意: 在实际应用中，使用错误的令牌的请求会被拒绝"
echo "      您可以查看服务日志 (data/service.log) 了解更多信息"
echo

# 测试增量更新
echo "5. 测试增量更新功能..."
./immutable_client update test_security.txt "这是通过rsync增量更新的内容"
echo

# 显示文件信息
echo "6. 显示文件信息..."
./immutable_client info test_security.txt
echo

# 模拟时间满足保留期
if command -v touch &> /dev/null; then
    echo "7. 模拟文件已达到保留期 (修改元数据文件时间)..."
    # 计算24小时前的时间戳
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        timestamp=$(date -v-25H "+%Y%m%d%H%M.%S")
        touch -t "$timestamp" data/test_security.txt.meta
    else
        # Linux
        timestamp=$(date -d "25 hours ago" "+%Y%m%d%H%M.%S")
        touch -t "$timestamp" data/test_security.txt.meta
    fi
    echo "元数据文件时间已修改，现在尝试删除..."
    ./immutable_client delete test_security.txt
else
    echo "无法模拟保留期，跳过删除测试"
fi

echo
echo "测试完成，请查看数据目录和日志文件了解更多细节"
echo "文件路径: data/"
echo "日志文件: data/service.log" 