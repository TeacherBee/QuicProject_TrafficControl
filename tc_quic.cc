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
#include <atomic>
#include <queue>
#include <fstream>
#include <memory>
#include <functional>
#include "tc_quic.hh"
#include <random>
//#include "ring_buffer.hh"
using namespace std;   



// --------------- NetworkSimulator 类实现 ---------------
NetworkSimulator::NetworkSimulator(TapInterface* t0, TapInterface* t1) 
    : tap0(t0), tap1(t1), running(false), paused(false), total_duration_ms(0) 
{
    // 设置初始参数为无限制
    tap0->set_bw(0);
    tap0->set_delay_ms(0);
    tap0->set_loss(0);
    
    tap1->set_bw(0);
    tap1->set_delay_ms(0);
    tap1->set_loss(0);
}

NetworkSimulator::~NetworkSimulator() {
    stop();
    if (sim_thread.joinable()) {
        sim_thread.join();
    }
}

void NetworkSimulator::addEvent(int64_t start_time_ms, int64_t duration_ms, 
                               int64_t bandwidth, int64_t delay_ms, int loss_rate, 
                               const std::string& desc) {
    event_queue.push(NetworkEvent(start_time_ms, duration_ms, bandwidth, delay_ms, loss_rate, desc));
}

void NetworkSimulator::setTotalDuration(int64_t duration_ms) {
    total_duration_ms = duration_ms;
}

void NetworkSimulator::start() {
    if (running) return;
    
    running = true;
    paused = false;
    simulation_start_time = tap0->get_ms();
    
    sim_thread = std::thread([this]() {
        runSimulation();
    });
}

void NetworkSimulator::pause() {
    paused = true;
}

void NetworkSimulator::resume() {
    paused = false;
}

void NetworkSimulator::stop() {
    running = false;
    if (sim_thread.joinable()) {
        sim_thread.join();
    }
}

void NetworkSimulator::runSimulation() {
    cout << "\n========== 网络仿真开始 ==========" << endl;
    cout << "总时长: " << total_duration_ms << " ms" << endl;
    cout << "事件数: " << event_queue.size() << endl;
    cout << "==================================" << endl;
    
    // 创建事件队列的副本用于处理
    auto events = event_queue;
    std::unique_ptr<NetworkEvent> current_event(nullptr);
    int64_t event_end_time = 0;
    int event_counter = 0;
    
    int64_t start_time = tap0->get_ms();
    int64_t last_print_time = start_time;
    
    while (running && (tap0->get_ms() - start_time) < total_duration_ms) {
        // 处理暂停
        while (paused && running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        int64_t current_time = tap0->get_ms() - start_time;
        
        // 检查当前事件是否结束
        if (current_event && current_time >= event_end_time) {
            cout << "[事件结束][" << current_time << "ms] " 
                 << current_event->description << endl;
            
            // 恢复为默认参数（无限制）
            tap0->set_bw(0);
            tap0->set_delay_ms(0);
            tap0->set_loss(0);
            
            tap1->set_bw(0);
            tap1->set_delay_ms(0);
            tap1->set_loss(0);
            
            current_event.reset(nullptr);
        }
        
        // 检查是否有新事件开始
        if (!events.empty() && current_time >= events.top().start_time_ms) {
            current_event = std::make_unique<NetworkEvent>(events.top());
            events.pop();
            
            event_end_time = current_event->start_time_ms + current_event->duration_ms;
            event_counter++;
            
            cout << "\n[事件开始 #" << event_counter << "][" << current_time << "ms] " 
                 << current_event->description << endl;
            cout << "  带宽: " << current_event->bandwidth << " bps" << endl;
            cout << "  延迟: " << current_event->delay_ms << " ms" << endl;
            cout << "  丢包: " << current_event->loss << "‰" << endl;
            cout << "  持续时间: " << current_event->duration_ms << " ms" << endl;
            
            // 应用事件参数
            // TODO(bannos)：这里考虑是否只设置tap0的参数，还是两个都设置
            tap0->set_bw(current_event->bandwidth);
            tap0->set_delay_ms(current_event->delay_ms);
            tap0->set_loss(current_event->loss);
            
            tap1->set_bw(current_event->bandwidth);
            tap1->set_delay_ms(current_event->delay_ms);
            tap1->set_loss(current_event->loss);
        }
        
        // 显示进度（每5秒一次）
        if (current_time - last_print_time >= 5000) {
            float progress = (float)current_time / total_duration_ms * 100;
            cout << "进度: " << fixed << setprecision(1) << progress << "% (" 
                 << current_time << " ms / " << total_duration_ms << " ms)" << endl;
            last_print_time = current_time;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 10ms检查间隔
    }
    
    // 仿真结束
    if (current_event) {
        current_event.reset(nullptr);
    }
    
    // 设置链路断开（将带宽设为极低，延迟设为极大，丢包设为100%）
    tap0->set_bw(0.01);  // 1 bps，几乎无法通信
    tap0->set_delay_ms(10000);  // 10秒延迟
    tap0->set_loss(1000);  // 100%丢包
    
    tap1->set_bw(0.01);
    tap1->set_delay_ms(10000);
    tap1->set_loss(1000);
    
    cout << "\n========== 网络仿真结束 ==========" << endl;
    cout << "总时长: " << total_duration_ms << " ms" << endl;
    cout << "处理事件: " << event_counter << " 个" << endl;
    cout << "链路已断开（带宽1bps，延迟10s，丢包100%）" << endl;
    cout << "==================================" << endl;
    
    running = false;
}

// --------------- 宏定义 ---------------
#define BUFFER_SIZE 1500        // 以太网MTU默认值（最大帧大小）
#define SYSTEM(A) system(A)     // 封装system调用（执行系统命令）

// --------------- 解析脚本文件函数 ---------------
/**
 * @brief 从脚本文件加载网络事件
 * @param filename 脚本文件名
 * @param simulator 网络仿真器
 * @return bool 是否成功加载
 */
bool loadScriptFromFile(const std::string& filename, NetworkSimulator& simulator) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开脚本文件: " << filename << std::endl;
        return false;
    }
    
    std::string line;
    int line_num = 0;
    int event_count = 0;
    
    std::cout << "加载脚本文件: " << filename << std::endl;
    
    while (std::getline(file, line)) {
        line_num++;
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        std::istringstream iss(line);
        int64_t start_time, duration, bandwidth, delay;
        int loss;
        std::string description;
        
        if (iss >> start_time >> duration >> bandwidth >> delay >> loss) {
            // 读取剩余部分作为描述
            std::getline(iss >> std::ws, description);
            
            simulator.addEvent(start_time, duration, bandwidth, delay, loss, description);
            event_count++;
            std::cout << "  事件" << event_count << ": " << start_time << "ms开始, " 
                      << duration << "ms, " << bandwidth << "bps, " 
                      << delay << "ms延迟, " << loss << "‰丢包" << std::endl;
        } else {
            std::cerr << "脚本文件第 " << line_num << " 行格式错误: " << line << std::endl;
        }
    }
    
    file.close();
    std::cout << "成功加载 " << event_count << " 个事件" << std::endl;
    return event_count > 0;
}

// --------------- TapInterface类实现（保持不变，除了新增方法）---------------
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
    std::uniform_int_distribution<> distr(1, 1000);
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
                // if(tap_name == "tap0") {
                //     if(delay_ms > 0)
                //     {
                //         cout << tap_name << " --- " << "当前设置延迟：" << delay_ms << " ms" << endl;
                //     }
                //     else {
                //         cout << tap_name << " --- " << "未设置延迟" << endl;
                //     }
                // }

                if(bandwidth > 0)
                {
                    // if(tap_name == "tap0")
                    //     cout << tap_name << " --- " << "带宽限制：" << bandwidth << endl;
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

                    // cout << "pre_time: " << pre_time << " -- send_time: " << send_time << " -- delay_ms: " << delay_ms << endl;
                    
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
                    // if(tap_name == "tap0")
                    //     cout << tap_name << " --- " << "未设置带宽限制，bandwidth is " << bandwidth << endl;
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
            // if(tap_name == "tap0")
            //     cout << tap_name << " --- " << "当前已设置丢包：" << Bloss << endl;
            // 仅对tap0生效 + 随机丢包
            if(chance_in_a_thousand(Bloss))
            {
                // cout << tap_name << " --- " << "Dropped packet" << endl; // 丢包日志
            }
            else // 不丢包：发送数据包到目标TAP接口
            {
                write(dst_fd, node->data, node->size);
            }
        }
        else // 关闭丢包：直接发送
        {
            // if(tap_name == "tap0")
            //     cout << tap_name << " --- " << "未设置丢包" << endl;
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
    std::cout << "Usage: ./tc_quic [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --srctap=<value>    Source Tap (default: tap0)" << std::endl;
    std::cout << "  --srceth=<value>    Source Eth (default: eth1_h)" << std::endl;
    std::cout << "  --srcbr=<value>     Source Bridge (default: aif)" << std::endl;
    std::cout << "  --dsttap=<value>    Destination Tap (default: tap1)" << std::endl;
    std::cout << "  --dsteth=<value>    Destination Eth (default: eth2_h)" << std::endl;
    std::cout << "  --dstbr=<value>     Destination Bridge (default: bif)" << std::endl;
    std::cout << "  --delay_ms=<value>  Initial delay in milliseconds (default: 0)" << std::endl;
    std::cout << "  --total_time=<ms>   Total simulation duration (ms), 0=interactive mode" << std::endl;
    std::cout << "  --script=<file>     Script file for network changes" << std::endl;
    std::cout << "  --demo              Run a built-in demo scenario" << std::endl;
    std::cout << "  -h, --help          Display this help message" << std::endl;
    std::cout << "\nInteractive mode commands (when total_time=0):" << std::endl;
    std::cout << "  b <value>  Set bandwidth (bps)" << std::endl;
    std::cout << "  r <value>  Set RTT (ms)" << std::endl;
    std::cout << "  l <value>  Set loss rate (‰)" << std::endl;
    std::cout << "  q          Quit interactive mode" << std::endl;
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

// --------------- 示例脚本生成函数 ---------------
/**
 * @brief 创建内置演示脚本
 * @param simulator 网络仿真器
 * @param total_duration_ms 总时长
 * @details 创建一个模拟网络拥塞、恢复、波动的完整场景
 */
void createDemoScenario(NetworkSimulator& simulator, int64_t total_duration_ms) {
    cout << "使用内置演示脚本..." << endl;
    
    // 正常网络 (0-10秒)
    simulator.addEvent(0,      10000,  100, 50,  0,   "正常网络: 100Mbps, 50ms延迟");
    
    // 轻度拥塞 (10-20秒)
    simulator.addEvent(10000,  10000,  50,  100, 20,  "轻度拥塞: 50Mbps, 100ms延迟, 2%丢包");
    
    // 网络波动 (20-30秒)
    simulator.addEvent(20000,  2000,   20,  200, 50,  "重度拥塞: 20Mbps, 200ms延迟, 5%丢包");
    simulator.addEvent(22000,  2000,   80,  150, 10,  "恢复中: 80Mbps, 150ms延迟, 1%丢包");
    simulator.addEvent(24000,  2000,   20,  250, 80,  "再次拥塞: 20Mbps, 250ms延迟, 8%丢包");
    simulator.addEvent(26000,  2000,   60,  120, 5,   "部分恢复: 60Mbps, 120ms延迟, 0.5%丢包");
    simulator.addEvent(28000,  2000,   40,  180, 30,  "中度拥塞: 40Mbps, 180ms延迟, 3%丢包");
    
    // 网络恢复 (30-40秒)
    simulator.addEvent(30000,  5000,   80,  100, 5,   "恢复: 80Mbps, 100ms延迟, 0.5%丢包");
    simulator.addEvent(35000,  5000,   100, 50,  0,   "完全恢复: 100Mbps, 50ms延迟");
    
    cout << "已创建演示脚本，包含10个网络事件" << endl;
}

// --------------- 主函数 ---------------
/**
 * @brief 程序入口函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码（0=成功，1=失败）
 */
int main(int argc, char **argv) 
{
    int opt; // 命令行参数解析临时变量
    // 默认参数：源/目标TAP/网卡/桥接接口
    string srctap="tap0", srceth="eth1_h", srcbr="aif", 
           dsttap="tap1", dsteth="eth2_h", dstbr="bif";
    int delay_ms = 0;
    int64_t total_time_ms = 0;
    string script_file;
    bool demo_mode = false;
    
    // 长命令行参数定义（getopt_long使用）
    struct option long_option[] = 
    {
        {"srctap",    required_argument, nullptr, 'a'},
        {"srceth",    required_argument, nullptr, 'b'},
        {"srcbr",     required_argument, nullptr, 'c'},
        {"dsttap",    required_argument, nullptr, 'd'},
        {"dsteth",    required_argument, nullptr, 'e'},
        {"dstbr",     required_argument, nullptr, 'f'},
        {"delay_ms",  required_argument, nullptr, 'g'},
        {"total_time",required_argument, nullptr, 't'},
        {"script",    required_argument, nullptr, 's'},
        {"demo",      no_argument,       nullptr, 'm'},
        {"help",      no_argument,       nullptr, 'h'},
        {nullptr,     0,                 nullptr, 0}
    };

    // 解析命令行参数
    while((opt = getopt_long(argc, argv, "a:b:c:d:e:f:g:t:s:mh", long_option, nullptr)) != -1) 
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
            case 't':
                total_time_ms = atoll(optarg);
                break;
            case 's':
                script_file = optarg;
                break;
            case 'm':
                demo_mode = true;
                break;
            case 'h':
                printHelp();
                return 0;
            default:
                return 1;
        }
    }

    // --------------- 初始化TAP接口 ---------------
    cout << "初始化TAP接口..." << endl;
    TapInterface tap0(srctap.c_str(), srcbr.c_str(), srceth.c_str(), 0, 100);
    TapInterface tap1(dsttap.c_str(), dstbr.c_str(), dsteth.c_str(), 100, 0);
    
    if (tap0.tap_open() < 0 || tap1.tap_open() < 0) {
        cerr << "无法打开TAP接口，请检查权限" << endl;
        return 1;
    }
    
    tap0.set_dstap(tap1.get_tap());
    tap1.set_dstap(tap0.get_tap());

    // 初始化链表
    tap0.addNode(nullptr, tap0.get_us(), tap1.get_tap(), 1522, tap0.get_us(), 0);
    tap1.addNode(nullptr, tap1.get_us(), tap0.get_tap(), 1522, tap1.get_us(), 0);

    // --------------- 创建工作线程 ---------------
    cout << "启动数据包处理线程..." << endl;
    thread t1(thread_function, &tap0);
    thread t2(thread_function, &tap1);
    
    // 给线程一点时间启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (total_time_ms > 0) {
        // --------------- 脚本仿真模式 ---------------
        NetworkSimulator simulator(&tap0, &tap1);
        simulator.setTotalDuration(total_time_ms);
        
        if (demo_mode) {
            // 使用内置演示脚本
            createDemoScenario(simulator, total_time_ms);
        } else if (!script_file.empty()) {
            // 从文件加载脚本
            if (!loadScriptFromFile(script_file, simulator)) {
                cerr << "脚本加载失败，使用交互模式" << endl;
                total_time_ms = 0; // 回退到交互模式
            }
        } else {
            // 创建简单测试脚本
            cout << "使用简单测试脚本..." << endl;
            // 简单脚本：正常 -> 拥塞 -> 恢复
            simulator.addEvent(0,      10000,  100, 50,  0,   "正常网络");
            simulator.addEvent(10000,  10000,  20,  200, 50,  "网络拥塞");
            simulator.addEvent(20000,  10000,  100, 50,  0,   "恢复网络");
        }
        
        if (total_time_ms > 0) {
            cout << "\n开始网络仿真，总时长: " << total_time_ms << " ms" << endl;
            simulator.start();
            
            // 等待仿真结束
            while (simulator.isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            cout << "仿真结束，等待线程退出..." << endl;
            // 等待工作线程结束
            t1.join();
            t2.join();
            
            return 0;
        }
    }
    
    // --------------- 交互式模式 ---------------
    cout << "\n========== 交互模式 ==========" << endl;
    cout << "可用命令:" << endl;
    cout << "  b <value>  - 设置带宽 (bps)" << endl;
    cout << "  r <value>  - 设置RTT (ms)" << endl;
    cout << "  l <value>  - 设置丢包率 (‰)" << endl;
    cout << "  q          - 退出程序" << endl;
    cout << "==============================" << endl;
    
    string line;
    while (getline(cin, line)) // 循环读取标准输入
    {
        if (line == "q" || line == "quit") {
            cout << "退出程序..." << endl;
            break;
        }
        
        auto parsed = parseInput(line); // 解析输入
        if(parsed.second == -1) // 解析失败
        {
            cout << "无效输入，格式应为: [b|r|l] <value>" << endl;
            continue;
        }
        else if(parsed.first == 'b') // 设置带宽（b + 数值）
        {
            tap0.set_bw(parsed.second);
            tap1.set_bw(parsed.second);
            cout << "带宽已改为: " << parsed.second << " Mbps" << endl;
        }
        else if(parsed.first == 'r') // 设置RTT（r + 数值）
        {
            // RTT是往返延迟，因此每个方向延迟为RTT/2
            tap1.set_delay_ms(parsed.second * 1000/2);
            tap0.set_delay_ms(parsed.second * 1000/2);
            cout << "RTT已改为: " << parsed.second << " ms (每个方向 " << parsed.second/2 << " ms)" << endl;
        }
        else if(parsed.first == 'l') // 设置丢包率（l + 数值）
        {
            tap0.set_loss(parsed.second);
            tap1.set_loss(parsed.second);
            cout << "丢包率已改为: " << parsed.second << "‰ (" << (parsed.second/10.0) << "%)" << endl;
        }
    }

    // 等待线程结束
    t1.join();
    t2.join();

    return 0;
}