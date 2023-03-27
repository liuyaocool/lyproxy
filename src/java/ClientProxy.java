import java.io.IOException;
import java.io.PrintStream;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.*;
import java.nio.charset.StandardCharsets;
import java.util.Iterator;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

/**
 * ByteBuffer : https://blog.csdn.net/xiao_yu_gan/article/details/124388655
 *
 * Selector.register 与 Selector.select 会抢锁: https://blog.csdn.net/hezuideda/article/details/46739633
 */
public class ClientProxy extends Thread {

    static final boolean logInfo = false;
    static final boolean logErr = true;

    static final char KEY_EN[] = new char[256], KEY_DE[] = new char[256];
    static final char ENCRYPT = 'E'; // 加密模式
    static final char DECRYPT = 'D'; // 解密模式
    static final byte[] CONNECT_OK = "HTTP/1.1 200 Connection Established\r\n\r\n".getBytes(StandardCharsets.UTF_8);

    static final BlockingQueue<SocketChannel> ACCEPT_QUEUE = new LinkedBlockingQueue<>();
    static String serverIp;
    static int serverPort;

    public static void main(String[] args) throws Exception {
        if (args.length < 4) {
            System.out.println("Usage: java ClientProxy <local_port> <remote_ip> <remote_port> <secret> (<worker_thread_num>)");
            System.exit(0);
        }
        int i = 0;
        int localPort = Integer.parseInt(args[i++]);
        serverIp = args[i++];
        serverPort = Integer.parseInt(args[i++]);
        key_init(args[i++]);
        int workers = args.length >= i ? Integer.parseInt(args[i]) : 10;

        // 开启端口
        ServerSocketChannel server = ServerSocketChannel.open();
//        server.configureBlocking(false);
        server.bind(new InetSocketAddress(localPort));

        // 初始化 工作线程
        for (i = 0; i < workers; i++) new ClientProxy().start();

        System.out.format("open：%d，connect to：%s:%s，workers：%d \n",
                localPort, serverIp, serverPort, workers);

        // 主线程 处理链接
        SocketChannel accept;
        for (;;) {
            accept = server.accept();
            logInfo("%s coming ^-^ ^-^ ^-^ ^-^", accept.getRemoteAddress());
            ACCEPT_QUEUE.put(accept);
        }
    }

    static final BlockingQueue<Object[]> LOG_QUEUE = new LinkedBlockingQueue<>();
    static {
        new Thread(() -> {
            for (;;) {
                try {
                    Object[] take = LOG_QUEUE.take();
                    ((PrintStream) take[0]).format(take[1] + "\n", (Object[]) take[2]);
                } catch (InterruptedException e) {
                }
            }
        }).start();
    }

    static void logInfo(String format, Object... param) {
        if (logInfo) LOG_QUEUE.add(new Object[] {System.out, format, param});
    }
    static void logErr(String format, Object... param) {
        if (logErr) LOG_QUEUE.add(new Object[] {System.err, format, param});
    }

    static int hexToInt(char c) {
        if (c > 47 && c < 58) return c - 48;
        if (c > 96 && c < 103) return c - 97 + 10;
        return -1;
    }

    static void key_init(String secret) {
        for (int i = 0; i < 256; i++) KEY_EN[i] = (char) (hexToInt(secret.charAt(i*2)) * 16 + hexToInt(secret.charAt(i*2+1)));
        for (int i = 0; i < 256; i++) KEY_DE[KEY_EN[i]] = (char) i;
    }

    static boolean startWith(CharSequence str, CharSequence prefix) {
        if (null == str || 0 == str.length()) return false;
        if (null == prefix || 0 == str.length()) return true;
        if (str.length() < prefix.length()) return false;
        for (int i = 0; i < prefix.length(); i++) {
            if (str.charAt(i) != prefix.charAt(i)) return false;
        }
        return true;
    }

    static ExecutorService connServerPoll = Executors.newCachedThreadPool();
    static void firstRecv(SelectionKey key, ClientProxy ev) {
        SocketChannel client = (SocketChannel) key.channel();

        Future<SocketChannel> remoteClient = connServerPoll.submit(() -> {
            SocketChannel open = SocketChannel.open();
            open.configureBlocking(false); // 配置非阻塞后边会拿不到连接
            open.connect(new InetSocketAddress(serverIp, serverPort));
            while (!open.finishConnect()) {
                Thread.sleep(100);
            }
            return open;
        });

        int read_len;
        if ((read_len = recv(client, ev, false)) <= 0){
            logErr("first read len: %s", read_len);
            try {
                close(key, client, remoteClient.get());
            } catch (Exception e) {
                e.printStackTrace();
            }
            return;
        }
        StringBuilder line = new StringBuilder(100);
        boolean isCR = false, isHttps = false;
        // get host port
        for (byte b : ev.bufData) {
            if (isCR && '\n' == b) {
                if ((isHttps = startWith(line, "CONNECT")) || startWith(line, "Host:")) {
                    break;
                }
                line = new StringBuilder(100);
                continue;
            }
            isCR = '\r' == b;
            line.append((char) b);
        }
        String hostPort = isHttps
                ? line.substring(8, line.indexOf(" ", 8))
                : line.substring(6, line.length()-1);
        hostPort += hostPort.contains(":") ? "\r\n" : (isHttps ? ":443\r\n" : ":80\r\n");
        ev.hostPort = hostPort;
        byte[] encode = encode(hostPort);
        SocketChannel server = null;
        try {
            server = remoteClient.get();
        } catch (Exception e) {
            e.printStackTrace();
            return;
        }
        // 发送 connect 请求
        send(server, ev.buf, encode, false);
        // 接收 connect 结果
        byte[] bufData = ev.bufData;
        while ((read_len = recv(server, ev, true)) <= 0) {
            try {Thread.sleep(100);} catch (InterruptedException e) {}
        }
        hostPort = hostPort.substring(0, hostPort.length() - 2);
        if ('2' != ev.bufData[0] || '0' != ev.bufData[1] || '0' != ev.bufData[2]) {
            logErr("recv(%s %d) connect(%s) code: %s",
                    server.isConnected(), read_len, hostPort, new String(ev.bufData));
            close(key, client, server);
            return;
        }
        ev.bufData = bufData;
        logInfo("connect over(%s): %d %s", isHttps ? "https" : "http", read_len, hostPort);

        ev.mode = ENCRYPT;
        ev.toCh = server;
        ev.handler = ClientProxy::transfer_data;
        // 此处会与 Selector.select() 抢锁
        try {
            server.configureBlocking(false);
            // 加入 selector
            server.register(ev.selector, SelectionKey.OP_READ,
                    new ClientProxy(client, DECRYPT, ByteBuffer.allocate(8192), ClientProxy::transfer_data, hostPort));
        } catch (IOException e) {
            e.printStackTrace();
        }
        // https 返回客户端, http继续转发
        if (isHttps) {
            send(client, ev.buf, CONNECT_OK, false);
        } else {
            logInfo("---send0---:\n%s", new String(ev.bufData, StandardCharsets.UTF_8));
            send(server, ev.buf, ev.bufData, true);
            // 剩余数据转发
            while ((read_len = recv(client, ev, false)) > 0) {
                send(server, ev.buf, ev.bufData, true);
                logInfo("---send1---:\n%s", new String(ev.bufData, StandardCharsets.UTF_8));
            }
            if (read_len < 0){
                logErr("%s first http read len: %d", hostPort, read_len);
                close(key, client);
            }
        }
    }

    static void transfer_data(SelectionKey key, ClientProxy ev) {
        SocketChannel client = (SocketChannel) key.channel();
//        if (!client.isConnected()) {
//            close(key, client, toChannel);
//            return;
//        }

        boolean ifRecvDecode = ev.mode == DECRYPT;
        boolean ifSendEncode = ev.mode == ENCRYPT;

        int read_len;
        while ((read_len = recv(client, ev, ifRecvDecode)) > 0) {
            send(ev.toCh, ev.buf, ev.bufData, ifSendEncode);
        }
        if (read_len < 0) {
            logErr("%s transfer data read len: %d", ev.hostPort, read_len);
            close(key, client);
        }
    }

    static byte[] encode(String buf) {
        byte[] bytes = new byte[buf.length()];
        for (int i = 0; i < buf.length(); i++) {
            bytes[i] = (byte) KEY_EN[buf.charAt(i)];
        }
        return bytes;
    }

    static byte code(char[] key, byte b) {
        return  (byte) key[b < 0 ? (b + 256) : b];
    }

    static int recv(SocketChannel client, ClientProxy ev, boolean ifDecode) {
        int read_len = -1;
        try {
            ev.buf.clear();
            if (!client.isConnected() || (read_len = client.read(ev.buf)) <= 0) {
                return read_len;
            }
        } catch (IOException e) {
            e.printStackTrace();
            return -1;
        }
        ev.buf.flip();
        byte[] buf = new byte[read_len];
        ev.buf.get(buf);
        if (ifDecode) {
            for (int i = 0; i < buf.length; i++) {
                buf[i] = code(KEY_DE, buf[i]);
            }
        }
        ev.bufData = buf;
        ev.buf.clear();
        return read_len;
    }

    static void send(SocketChannel server, ByteBuffer buffer, byte[] buf, boolean ifEncode) {
        if (ifEncode) {
            for (int i = 0; i < buf.length; i++) {
                buf[i] = code(KEY_EN, buf[i]);
            }
        }
        buffer.clear();
        buffer.put(buf).flip();
        try {
            if (server.isConnected()) server.write(buffer);
        } catch (IOException e) {
            // e.printStackTrace();
            logErr("send err: %s", e.getMessage());
        }
        buffer.clear();
    }

    static void close(SelectionKey key, SocketChannel... ch) {
        key.cancel(); // 从selector 注销
        ClientProxy event = (ClientProxy) key.attachment();
        if (null == ch) return;
        for (SocketChannel c : ch) {
            if (null == c) continue;
            try {
                logInfo("close: %s %s -> %s", event.hostPort, c.getLocalAddress(), c.getRemoteAddress());
                c.close();
            } catch (Exception e) {
                e.printStackTrace();
            }
        }
    }

    // ================= Worker thread ===========================
    Selector selector;

    public ClientProxy() throws IOException {
        selector = Selector.open();
    }

    @Override
    public void run() {
        SelectionKey key;
        Iterator<SelectionKey> iter;
        int select;
        SocketChannel ch;
        for (;;) {
            try {
                ch = ACCEPT_QUEUE.poll(50, TimeUnit.MILLISECONDS);
                if (null != ch) {
                    ch.configureBlocking(false);
                    ClientProxy att = new ClientProxy(null, ENCRYPT,
                            ByteBuffer.allocate(8192), ClientProxy::firstRecv, "");
                    att.selector = this.selector;
                    long t = System.currentTimeMillis();
                    logInfo("registry start %s", selector);
                    ch.register(selector, SelectionKey.OP_READ, att);
                    logInfo("registry end(%s ms) %s：%s -> %s", System.currentTimeMillis() - t, selector, ch.getLocalAddress(), ch.getRemoteAddress());
                }
                if ((select = selector.select(100)) <= 0) continue;
            } catch (Exception e) {
                e.printStackTrace();
                continue;
            }
            logInfo("selected %s", select);
            iter = selector.selectedKeys().iterator();
            while (iter.hasNext()) {
                key = iter.next();
                iter.remove();
                if (key.isReadable()) {
                    ((ClientProxy) key.attachment()).doHandler(key);
                }
            }
        }
    }

    // ================= Event ===========================
    public SocketChannel toCh; // 转发到
    public char mode; // 'E'：加密  'D'：解密
    public ByteBuffer buf; // 缓冲
    public byte[] bufData;
    public BiConsumer<SelectionKey, ClientProxy> handler; // 处理函数
    public String hostPort; // 请求的域名端口

    public ClientProxy(SocketChannel toCh, char mode, ByteBuffer allocate,
                       BiConsumer<SelectionKey, ClientProxy> handler, String hostPort) {
        this.toCh = toCh;
        this.mode = mode;
        this.buf = allocate;
        this.handler = handler;
        this.hostPort = hostPort;
    }

    static final AtomicLong l = new AtomicLong(0);
    public void doHandler(SelectionKey key) {
        long id = l.getAndIncrement();
        logInfo("--- %s >>> handler start %s", id, this.hostPort);
        this.handler.accept(key, this);
        logInfo("--- %s ===handler end %s", id, this.hostPort);
    }

}