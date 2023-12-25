# lyproxy

# 前提

## linux

gcc make

- Ubuntu: ` apt install gcc make `
- Termux(安卓): ` pkg install gcc make `
- ish (IOS): ` apk add gcc make musi-dev `
- Gentoo: ` emerge --ask gcc make `
- ArchLinux: ` pacman -S gcc make `
- ...

# 生成16进制乱序串

```shell
make key_gen
```

# server 端启动

```shell
# 编译
make server
# 启动
./bin/proxy_server 本地端口 乱序串
```

# client 端 (c)

支持 linux, mac, android(termux), ios(ish)

```shell
# 编译
make client
# 启动
./bin/proxy_client 本地端口 server端ip server端端口 乱序串
```

# client 端 (java)

## 命令行

```shell
cd src/java
javac ClientProxy.java
java ClientProxy 本地端口 server端ip server端端口 乱序串
```

## idea

<img src="doc/client_java_idea.png">

# ish

- 官网: https://www.alpinelinux.org/
- 头文件库: https://pkgs.alpinelinux.org/contents?branch=edge&name=musl%2Ddev&arch=armhf&repo=main

# window

函数库: https://learn.microsoft.com/zh-cn/windows/win32/api/winsock2/nf-winsock2-inet_addr