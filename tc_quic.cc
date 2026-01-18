#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <getopt.h>
#include <thread>
#include <arpa/inet.h>
#include <sstream>
#include <string>
#include <vector>
#include "tc_quic.hh"
#include <random>
//#include "ring_buffer.hh"
using namespace std;   




// --------------- 宏定义 ---------------
#define BUFFER_SIZE 1500        // 以太网MTU默认值（最大帧大小）
#define SYSTEM(A) system(A)     // 封装system调用（执行系统命令）


// --------------- TapInterface类实现 ---------------
/**
 * @brief TapInterface构造函数
 * @param tap_name TAP接口名（如tap0）
 * @param br_name 桥接接口名（如aif）
 * @param eth_name 物理网卡名（如eth2_h）
 * @param delay_time 初始延迟（毫秒）
 * @param bandwidth 初始带宽（bps）
 * @details 初始化参数 + 清理旧的桥接配置（防止残留）
 */
TapInterface::TapInterface(const char *tap_name, const char *br_name, const char *eth_name, int64_t delay_time = 200,int64_t bandwidth = 100)
{
    // 初始化成员变量
    this->delay_ms = delay_time;
    this->tap_name = tap_name;
    this->br_name = br_name;
    this->eth_name = eth_name;
    this->tap_fd = -1;          // 初始化为无效fd
    this->bandwidth = bandwidth;
    this->pre_time = 0;         // 上一个包发送时间初始化为0
    this->packet_cnt = 0;       // 数据包计数初始化为0
    this->Bloss = 0;        // 丢包率初始化为0（关闭丢包）

    // --------------- 清理旧的桥接配置 ---------------
    // 1. 关闭旧桥接接口
    string iptables_cmd ="ifconfig " + this->br_name + " down";
    cout << "iptables:: " << iptables_cmd << endl;
    SYSTEM (iptables_cmd.c_str());
    iptables_cmd.clear();

    // 2. 从桥接接口删除旧TAP接口
    iptables_cmd = "brctl delif " + this->br_name + " " + this->tap_name;
    cout << "iptables:: " << iptables_cmd << endl;
    SYSTEM (iptables_cmd.c_str());
    iptables_cmd.clear();

    // 3. 删除旧桥接接口
    iptables_cmd = "brctl delbr " + this->br_name;
    cout << "iptables:: " << iptables_cmd << endl;
    SYSTEM (iptables_cmd.c_str());    
    iptables_cmd.clear();
}

/**
 * @brief 获取当前时间戳（毫秒级）
 * @return int64_t 从epoch到现在的毫秒数
 */
int64_t TapInterface::get_ms()
{
    auto now = std::chrono::high_resolution_clock::now();
    // 获取时间戳，毫秒表示
    auto ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    auto ms_count = ms.time_since_epoch().count();
    return ms_count;
}

/**
 * @brief 获取当前时间戳（微秒级）
 * @return int64_t 从epoch到现在的微秒数
 * @note 流量控制需要更高精度，因此主要使用微秒级时间戳
 */
int64_t TapInterface::get_us()
{
    auto now = std::chrono::high_resolution_clock::now();
    auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    auto value = now_us.time_since_epoch().count();
    return value;
}

/**
 * @brief TapInterface析构函数
 * @details 关闭文件描述符 + 释放链表所有节点（防止内存泄漏）
 */
TapInterface::~TapInterface()
{
    close(tap_fd);
    close(epoll_fd);
    for(head; head != nullptr; head = head->next)
    {
        freeNode(head, dst_fd);
    }
}

/**
 * @brief 调试函数：打印数据包的十六进制内容
 * @param data 数据包二进制数据
 * @param size 数据包大小
 * @note 每8个字节换行，便于查看
 */
void TapInterface::printData(const unsigned char* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << "0x" << static_cast<int>(data[i]) << " ";
        if ((i + 1) % 8 == 0)
            std::cout << std::endl;
    }
    std::cout << std::dec;  // Reset to decimal format if needed
}

/**
 * @brief 随机丢包判断函数
 * @param chance 丢包概率（千分比，如10=1%）
 * @return bool true=丢包，false=不丢包
 * @note 使用mt19937随机数生成器，范围1-10000（精度更高）
 */
bool TapInterface::chance_in_a_thousand(int chance) {
    std::random_device rd;
    std::mt19937 gen(rd());
    // TODO(bannos): 这里确认一下到底是千分之一还是万分之一
    std::uniform_int_distribution<> distr(1, 10000);
    return distr(gen) <= chance;
};

/**
 * @brief 从TAP接口读取数据包（epoll监听）
 * @return int epoll_wait返回的事件数（-1=失败，0=无事件，>0=事件数）
 * @details 1. 监听TAP接口可读事件 2. 读取数据包 3. 计算发送时间 4. 加入链表缓存
 */
int TapInterface::tap_read()
{
    int timeout = 0;           // epoll_wait超时时间（0=非阻塞）
    int64_t send_time;         // 数据包计划发送时间（微秒）
    // 监听epoll事件：无超时（非阻塞）
    int eNum = epoll_wait(epoll_fd, events, MAX_EVENTS, timeout);
    if(eNum == -1)             // epoll_wait失败
    {
        cout << "epoll wait" << endl;
        return -1;
    }

    // 遍历所有触发的事件
    for(int i = 0; i < eNum; i++)
    {
        // 仅处理TAP接口的可读事件
        if(events[i].data.fd == tap_fd)
        {
            if(events[i].events & EPOLLIN) // 可读事件
            {
                uint8_t *data = new uint8_t[1522];  // 分配数据包缓冲区（1522=以太网最大帧大小+VLAN标签）
                uint32_t size = read(tap_fd, data,1522);    // 从TAP接口读取数据
                // 调试：打印数据包大小/内容
                //cout << "size: " << size << endl;
                //printData(data, size);
                if(size == -1) // 读取失败
                {
                    cout << "Error reading from tap_fd" << endl;
                }

                // --------------- 解析MAC帧类型 ---------------
                // MAC帧头部第12-13字节是帧类型（如0x0800=IP，0x0806=ARP）
                uint16_t* mac_type_ptr = reinterpret_cast<uint16_t*>(data + 12);
                uint16_t mac_type = ntohs(*mac_type_ptr); // 网络字节序转主机字节序
                int64_t time_now = get_us();

                // --------------- 带宽限制计算 ---------------
                // debug
                if(tap_name == "tap0") {
                    if(delay_ms > 0)
                    {
                        cout << tap_name << " --- " << "当前设置延迟：" << delay_ms << " ms" << endl;
                    }
                    else {
                        cout << tap_name << " --- " << "未设置延迟" << endl;
                    }
                }

                if(bandwidth > 0)
                {
                    if(tap_name == "tap0")
                        cout << tap_name << " --- " << "带宽限制：" << bandwidth << endl;
                    packet_cnt++;
                    if(packet_cnt%1000 == 0)
                    {
                        //cout << NodeCount << endl;
                        //cout << "Packet count: " << packet_cnt << endl;
                        // delete data;
                        // continue;
                    }
                    // 计算发送时间：上一个包发送时间 + 本包传输耗时（size/(带宽/8)）即传输时延
                    // 带宽单位是bps，除以8转换为Bps（字节/秒）
                    send_time = pre_time + (size*1.0/(bandwidth*1.0/8.0));
                    // double transmission_time_us = (size * 8.0 * 1000000.0) / bandwidth;
                    // send_time = pre_time + static_cast<int64_t>(transmission_time_us);

                    cout << "pre_time: " << pre_time << " -- send_time: " << send_time << " -- delay_ms: " << delay_ms << endl;
                    
                    if(send_time < time_now)
                    {
                        send_time = time_now;
                    }
                    pre_time = send_time;                // 更新上一个包发送时间
                    
                    

                    send_time = send_time + delay_ms;    // 叠加延迟时间
                    
                    //cout << "send_time: " << send_time << " time_now: " << time_now << endl;
                }
                else // 关闭带宽限制：仅叠加延迟
                {
                    if(tap_name == "tap0")
                        cout << tap_name << " --- " << "未设置带宽限制，bandwidth is " << bandwidth << endl;
                    send_time = time_now + delay_ms;
                }
                
                // --------------- 限流检查 ---------------
                if(NodeCount > MAX_PACKET_SIZE) // 超过最大缓存数，丢弃数据包
                {
                    //cout << "NodeCount: " << NodeCount << endl;
                    delete data;
                    return -1;
                }
                else // 加入链表缓存
                {
                    addNode(data,send_time,dst_fd,size,get_us(),mac_type);
                }

            }
        }
    }
    return eNum;
}

/**
 * @brief 重写释放节点函数（核心：发送数据包 + 丢包控制）
 * @param node 待释放的节点
 * @param dst_fd 目标TAP接口fd
 * @details 1. 按丢包率判断是否发送 2. 发送数据包 3. 释放内存
 */
void TapInterface::freeNode(Node *node, int dst_fd) 
{
    if(node->data != nullptr) 
    {
        if(Bloss > 0) // 开启丢包
        {
            if(tap_name == "tap0")
                cout << tap_name << " --- " << "当前已设置丢包：" << Bloss << endl;
            // 仅对tap0生效 + 随机丢包
            if(chance_in_a_thousand(Bloss) && tap_name == "tap0")
            {
                cout << tap_name << " --- " << "Dropped packet" << endl; // 丢包日志
            }
            else // 不丢包：发送数据包到目标TAP接口
            {
                write(dst_fd, node->data, node->size);
            }
        }
        else // 关闭丢包：直接发送
        {
            if(tap_name == "tap0")
                cout << tap_name << " --- " << "未设置丢包" << endl;
            write(dst_fd, node->data, node->size);
        }
        delete node->data; // 释放数据包内存
    }
    NodeCount--;
}

/**
 * @brief 主动发送超时的数据包（调用checkAndFreeNode释放节点）
 * @details 核心逻辑：检查链表中达到发送时间的节点，释放（发送）它们
 */
void TapInterface::tap_write()
{
    int64_t time = get_us();
    checkAndFreeNode(time, dst_fd);
}

/**
 * @brief 设置目标TAP接口fd（转发目标）
 * @param fd 目标TAP接口的文件描述符
 */
void TapInterface::set_dstap(int fd)
{
    this->dst_fd = fd; 
}

/**
 * @brief 获取当前TAP接口的fd
 * @return int TAP接口文件描述符
 */
int TapInterface::get_tap()
{
    return this->tap_fd;
}

/**
 * @brief 创建并配置TAP接口
 * @return int TAP接口fd（-1=失败，>0=成功）
 * @details 1. 创建epoll实例 2. 打开TUN/TAP设备 3. 配置TAP接口 4. 创建桥接 5. 将TAP/物理网卡加入桥接
 */
int TapInterface::tap_open()
{
    char tapname[14];    // TAP接口名缓冲区
    int i, fd,err;

    // 1. 创建epoll实例（参数1：忽略，仅需大于0）
    epoll_fd = epoll_create(1);
    if(epoll_fd == -1)
    {
        cout << "Error creating epoll instance" << endl;
        return -1;
    }
    event.events = EPOLLIN;    // 监听可读事件
    event.data.fd = epoll_fd;  // 绑定epoll fd

    // 2. 打开TUN/TAP设备（Linux内核虚拟网络设备）
    struct ifreq ifr;          // 网络接口请求结构体
    if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
    {
        return fd;
    }
    memset(&ifr, 0, sizeof(ifr)); // 初始化结构体
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;    // 配置为TAP模式（二层以太网接口）+ 无数据包信息头（IFF_NO_PI）
    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0)    // 设置TUN/TAP接口参数
    {
        close(fd);
        return err;
    }

    // 3. 设置非阻塞模式
    if (fcntl (fd, F_SETFL, O_NDELAY) > 0)
        cout << "fcntl problem" << endl;

    // 4. 设置进程为fd的属主（接收信号）
    if (fcntl (fd, F_SETOWN, getpid ()) > 0)
        cout << "fcntl problem" << endl;

    // 5. 配置成功：初始化TAP接口
	if( fd > 0 ) 
    {
        tap_fd = fd;                              // 保存TAP fd
        tap_name = ifr.ifr_name;                  // 保存TAP接口名（内核分配，如tap0）

        // --------------- 配置桥接 ---------------
        // 5.1 启用TAP接口
        string iptables_cmd = "ip link set dev " + this->tap_name + " up"; 
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 5.2 创建桥接接口
        iptables_cmd = "brctl addbr " + this->br_name;
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 5.3 将TAP接口加入桥接
        iptables_cmd = "brctl addif " + this->br_name + " " + this->tap_name;
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 5.4 将物理网卡加入桥接
        iptables_cmd = "brctl addif " + this->br_name + " " + this->eth_name;
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 5.5 关闭桥接的STP（生成树协议，避免延迟）
        iptables_cmd = "brctl stp " + this->br_name + " off";
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 5.6 启用桥接接口
        iptables_cmd = "ifconfig " + this->br_name + " up";
        cout << "iptables:: " << iptables_cmd << endl;
        SYSTEM (iptables_cmd.c_str());    
        iptables_cmd.clear();

        // 6. 将TAP fd添加到epoll监听
        event.events = EPOLLIN;
        event.data.fd = fd;
        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1)
        {
            cout << "Error adding tap_fd to epoll" << endl;
            close(fd);
            close(epoll_fd);
            return -1;
        }
        
		return fd;
	} 
    else 
    {
		return -1;
	}
}

void TapInterface::set_delay_ms(int64_t delay_ms)
{
    this->delay_ms = delay_ms;
}

void TapInterface::set_bw(int64_t bandwidth)
{
    this->bandwidth = bandwidth;
}

void TapInterface::set_loss(int loss)
{
    this->Bloss = loss;
}

void printHelp() {
    std::cout << "Usage: ./program [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --srctap=<value>    Source Tap" << std::endl;
    std::cout << "  --srceth=<value>    Source Eth" << std::endl;
    std::cout << "  --srcbr=<value>     Source Bridge" << std::endl;
    std::cout << "  --dsttap=<value>    Destination Tap" << std::endl;
    std::cout << "  --dsteth=<value>    Destination Eth" << std::endl;
    std::cout << "  --dstbr=<value>     Destination Bridge" << std::endl;
    std::cout << "  --delay_ms=<value>  Delay in milliseconds" << std::endl;
    std::cout << "  -h, --help          Display this help message" << std::endl;
}

/**
 * @brief 线程函数：循环读取并发送数据包
 * @param tap TapInterface对象指针
 * @details 无限循环：读取数据包 → 发送超时数据包
 */
void thread_function(TapInterface *tap)
{

    while(true)
    {
        tap->tap_read();
        tap->tap_write();
    }
}

/**
 * @brief 解析输入字符串为整数（已注释：未实际使用）
 * @param line 输入字符串
 * @return int64_t 解析后的整数（-1=失败）
 */
int64_t readInput(string line) 
{
    try 
    {
        int64_t value = std::stoi(line);
        std::cout << "Received value: " << value << std::endl;
        return value;
    } 
    catch (std::invalid_argument& e) 
    {
        std::cerr << "Invalid input: " << line << std::endl;
        return -1;
    }
    return -1;
}

/**
 * @brief 解析交互式输入（前缀+值）
 * @param line 输入行（如"b 100"表示设置带宽为100Mbps）
 * @return pair<char, int64_t> 前缀字符 + 数值（-1=解析失败）
 * @details 支持的前缀：b=带宽，r=RTT，l=丢包率
 */
std::pair<char, int64_t> parseInput(const std::string& line) {
    std::istringstream iss(line); // 字符串流
    char prefix;                  // 前缀字符（b/r/l）
    int64_t value;                // 数值

    if (iss >> prefix >> value) { // 解析前缀和数值
        return {prefix, value};
    } else {
        // handle error, return a default value or throw an exception
        return {' ', -1};
    }
}

// --------------- 主函数 ---------------
/**
 * @brief 程序入口函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码（0=成功，1=失败）
 * @details 1. 解析命令行参数 2. 创建两个TAP接口对象 3. 配置桥接和转发 4. 创建线程处理数据包 5. 交互式修改流量参数
 */
int main(int argc, char **argv) 
{
    int opt; // 命令行参数解析临时变量
    // 默认参数：源/目标TAP/网卡/桥接接口
    string srctap="tap0", srceth="eth1_h", srcbr="aif", dsttap="tap1", dsteth="eth2_h", dstbr="bif";
    int delay_ms;
    
    // 长命令行参数定义（getopt_long使用）
    struct option long_option[] = 
    {
        {"srctap",   required_argument, nullptr, 'a'},
        {"srceth",   required_argument, nullptr, 'b'},
        {"srcbr",    required_argument, nullptr, 'c'},
        {"dsttap",   required_argument, nullptr, 'd'},
        {"dsteth",   required_argument, nullptr, 'e'},
        {"dstbr",    required_argument, nullptr, 'f'},
        {"delay_ms", required_argument, nullptr, 'g'},
        {"help",     no_argument,       nullptr, 'h'},
        {nullptr,    0,                 nullptr, 0}
    };

    // 解析命令行参数
    while((opt = getopt_long(argc, argv, "a:b:c:d:e:f:g:h", long_option, nullptr)) != -1) 
    {
        switch(opt) 
        {
            case 'a':
                srctap = optarg;
                break;
            case 'b':
                srceth = optarg;
                break;
            case 'c':
                srcbr = optarg;
                break;
            case 'd':
                dsttap = optarg;
                break;
            case 'e':
                dsteth = optarg;
                break;
            case 'f':
                dstbr = optarg;
                break;
            case 'g':
                delay_ms = atoi(optarg);
                break;
            case 'h':
                printHelp();
                return 0;
            default:
                return 1;
        }
    }

    // --------------- 初始化TAP接口 ---------------
    // 创建源TAP接口对象（tap0）：初始延迟0ms，带宽100bps
    TapInterface tap0(srctap.c_str(), srcbr.c_str(), srceth.c_str(), 0, 100);
    // 创建目标TAP接口对象（tap1）：初始延迟100ms，带宽0（无限制）
    TapInterface tap1(dsttap.c_str(), dstbr.c_str(), dsteth.c_str(), 100, 0);
    // 打开并配置TAP接口
    tap0.tap_open();
    tap1.tap_open();
    
    // 设置转发目标：tap0的数据包转发到tap1，tap1的数据包转发到tap0
    tap0.set_dstap(tap1.get_tap());
    tap1.set_dstap(tap0.get_tap());

    // 初始化链表（添加空节点，注释：可能是调试用）
    tap0.addNode(nullptr,tap0.get_us(),tap1.get_tap(),1522,tap0.get_us(),0);
    tap1.addNode(nullptr,tap1.get_us(),tap0.get_tap(),1522,tap1.get_us(),0);

    // --------------- 创建工作线程 ---------------
    // 线程1：处理tap0的数据包读写
    thread t1(thread_function, &tap0);
    // 线程2：处理tap1的数据包读写
    thread t2(thread_function, &tap1);

    // --------------- 交互式修改流量参数 ---------------
    string line; // 输入行
    while (getline(cin, line)) // 循环读取标准输入
    {
        auto parsed = parseInput(line); // 解析输入
        if(parsed.second == -1) // 解析失败
        {
            continue;
        }
        else if(parsed.first == 'b') // 设置带宽（b + 数值）
        {
            tap0.set_bw(parsed.second);
            cout << "BW is changed: " << parsed.second << endl;
        }
        else if(parsed.first == 'r') // 设置RTT（r + 数值）
        {
            // RTT是往返延迟，因此每个方向延迟为RTT/2（毫秒转微秒？注：原代码*1000/2，可能是单位转换）
            tap1.set_delay_ms(parsed.second * 1000/2);
            tap0.set_delay_ms(parsed.second * 1000/2);
            cout << "RTT is changed: " << parsed.second << endl;
        }
        else if(parsed.first == 'l') // 设置丢包率（l + 数值）
        {
            tap0.set_loss(parsed.second);
            cout << "Loss rate set to: " << parsed.second << "/10000" << endl;
        }
    }

    // 等待线程结束（实际不会执行，因为线程是无限循环）
    t1.join();
    t2.join();

    return 0;
}