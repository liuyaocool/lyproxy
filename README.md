# lyproxy

# 前提

## linux 

需要gcc make

- ubuntu: ` apt install gcc make `

# 生成16进制乱序串

```shell
cd src
make key_gen
```

# server 端启动

```shell
cd src
# 编译
make server
# 启动
./bin/server_proxy 本地端口 乱序串
```

# client 端 (linux)

```shell
cd src
# 编译
make client
# 启动
./bin/client_proxy 本地端口 server端ip server端端口 乱序串
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