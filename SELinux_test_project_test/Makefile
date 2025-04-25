CC=gcc
CFLAGS=-Wall -Wextra -I./src
LDFLAGS_SELINUX=-lselinux
LDFLAGS_AUDIT=-laudit

TARGETS=immutable_service immutable_client

.PHONY: all clean install setup

all: $(TARGETS)

# 构建特权服务
immutable_service: src/immutable_service.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS_SELINUX)

# 构建客户端
immutable_client: src/immutable_client.c
	$(CC) $(CFLAGS) -o $@ $< -DCLIENT_MAIN

# 安装SELinux策略模块(需要root权限)
policy-install:
	@echo "构建并安装SELinux策略模块..."
	cd policy && make -f /usr/share/selinux/devel/Makefile immutable_policy.pp
	cd policy && sudo semodule -i immutable_policy.pp
	@echo "SELinux策略已安装"

# 设置文件上下文
setup-context:
	@echo "设置数据目录上下文..."
	sudo chcon -t immutable_data_dir_t data/
	sudo chcon -t immutable_service_exec_t immutable_service
	@echo "上下文已设置"

# 完整安装(需要root权限)
install: all policy-install setup-context
	@mkdir -p data
	@echo "安装完成. 现在您可以运行:"
	@echo "  ./immutable_service (作为服务端)"
	@echo "  ./immutable_client modify test.txt \"测试内容\" (作为客户端)"

# 启动服务
start-service:
	./immutable_service &

# 清理
clean:
	rm -f $(TARGETS)
	rm -f policy/*.pp

# 运行示例
example: all
	@echo "启动服务..."
	./immutable_service &
	@sleep 1
	@echo ""
	@echo "创建测试文件:"
	./immutable_client modify test.txt "这是一个不可变文件测试"
	@echo ""
	@echo "获取文件信息:"
	./immutable_client info test.txt
	@echo ""
	@echo "尝试增量更新:"
	./immutable_client update test.txt "这是一个通过rsync更新的不可变文件"
	@echo ""
	@echo "尝试删除文件 (将会失败，因为未达到保留期):"
	./immutable_client delete test.txt
	@echo ""
	@echo "关闭服务..."
	pkill immutable_service 