#ifndef TC_HH_
#define TC_HH_

#include <sys/epoll.h>
#include <string>
#include <time.h>
#include <atomic>
#include <thread>
#include <queue>
#include <functional>

// --------------- 全局宏定义 ---------------
/**
 * @def MAX_EVENTS
 * @brief epoll_wait一次最多监听的事件数量
 * @note epoll是Linux下高效的I/O多路复用机制，用于监听TAP接口的可读事件
 */
#define MAX_EVENTS 10

/**
 * @def MAX_PACKET_SIZE
 * @brief 链表允许缓存的最大数据包节点数（防止内存溢出）
 */
#define MAX_PACKET_SIZE 2048000

// --------------- 网络事件结构体 ---------------
/**
 * @struct NetworkEvent
 * @brief 网络特性变化事件定义
 */
struct NetworkEvent {
    int64_t start_time_ms;   // 开始时间（毫秒）
    int64_t duration_ms;     // 持续时间（毫秒）
    int64_t bandwidth;       // 带宽（Mbps）
    int64_t delay_ms;        // 延迟（毫秒）
    int loss;                // 丢包率（千分比）
    std::string description; // 事件描述
    
    NetworkEvent(int64_t start = 0, int64_t dur = 0, int64_t bw = 0, 
                 int64_t delay = 0, int loss_rate = 0, const std::string& desc = "")
        : start_time_ms(start), duration_ms(dur), bandwidth(bw), 
          delay_ms(delay), loss(loss_rate), description(desc) {}
    
    // 用于优先队列排序（按开始时间从小到大）
    bool operator>(const NetworkEvent& other) const {
        return start_time_ms > other.start_time_ms;
    }
};

// --------------- 网络仿真控制器 ---------------
class NetworkSimulator {
private:
    class TapInterface* tap0;
    class TapInterface* tap1;
    std::atomic<bool> running;
    std::atomic<bool> paused;
    std::thread sim_thread;
    int64_t total_duration_ms;
    std::priority_queue<NetworkEvent, std::vector<NetworkEvent>, 
                       std::greater<NetworkEvent>> event_queue;
    int64_t simulation_start_time;
    
public:
    NetworkSimulator(class TapInterface* t0, class TapInterface* t1);
    ~NetworkSimulator();
    
    void addEvent(int64_t start_time_ms, int64_t duration_ms, int64_t bandwidth,
                  int64_t delay_ms, int loss_rate, const std::string& desc = "");
    void setTotalDuration(int64_t duration_ms);
    void start();
    void pause();
    void resume();
    void stop();
    bool isRunning() const { return running; }
    bool isPaused() const { return paused; }
    
private:
    void runSimulation();
};

// --------------- 链表节点类（缓存网络数据包） ---------------
/**
 * @class ListNode
 * @brief 双向链表基类，用于缓存待发送的网络数据包
 * @details 每个节点存储数据包内容、发送时间、套接字、大小等信息，支持节点添加/释放/超时检查
 */
class ListNode
{
public:
    /**
     * @struct Node
     * @brief 链表节点结构体，存储单个网络数据包的完整信息
     */
    struct Node
    {
        uint8_t *data;          // 数据包原始数据（二进制）
        int64_t sendtime;       // 数据包计划发送时间（微秒级时间戳）
        int64_t timesample;     // 数据包接收时间戳（微秒级）
        uint32_t sock;          // 目标发送套接字（TAP接口fd）
        uint32_t size;          // 数据包字节大小
        uint16_t mac_type;      // MAC帧类型（如0x0800=IP协议）
        struct Node *next;      // 下一个节点指针（单链表）
        Node(uint8_t *data, int64_t time, uint32_t sock, uint32_t size, 
             int64_t timesample, uint16_t mac_type):
            data(data),sendtime(time),timesample(timesample),sock(sock),
            size(size),next(nullptr),mac_type(mac_type){}
    };
    Node *head = nullptr;   // 链表头节点
    Node *tail = nullptr;   // 链表尾节点（优化尾插效率，无需遍历）
    int NodeCount = 0;      // 链表当前节点数（用于限流）

    /**
     * @brief 向链表尾部添加新节点（数据包）
     * @param data 数据包数据指针
     * @param time 计划发送时间（微秒）
     * @param sock 目标套接字fd
     * @param size 数据包大小
     * @param timesample 接收时间戳
     * @param mac_type MAC帧类型
     */
    void addNode(uint8_t *data, int64_t time, uint32_t sock, uint32_t size, 
                 int64_t timesample, uint16_t mac_type)
    {
        Node * newnode = new Node(data, time, sock, size, timesample, mac_type);
        if(head == nullptr) // 空链表：头尾都指向新节点
        {
            head = newnode;
            tail = newnode;
            return;
        }
        else
        {
            tail->next = newnode;
            tail = newnode;
        }
        NodeCount++;
    }

    /**
     * @brief 虚函数：释放单个节点（可被子类重写，实现自定义释放逻辑）
     * @param node 待释放的节点指针
     * @param dst_fd 目标发送fd（预留参数）
     */
    virtual void freeNode(Node *node, int dst_fd)
    {
        // 释放数据包内存
        if(node->data != nullptr)
        {
            delete node->data;
        }
        // 释放节点本身
        if(node != nullptr)
        {
            delete node;
        }
        NodeCount--;
    }

    /**
     * @brief 检查并释放超时节点（达到发送时间的数据包）
     * @param time 当前时间戳（微秒）
     * @param dst_fd 目标发送fd
     * @note 遍历链表，释放所有sendtime <= 当前时间的节点（子类重写freeNode实现发送逻辑）
     */
    void checkAndFreeNode(int64_t time, uint32_t dst_fd)
    {
        if(head == nullptr) // 空链表直接返回
        {
            return;
        }
        // 从第二个节点开始遍历（prev指向第一个节点）
        Node *cur = head->next;
        Node *prev = head;
        while(cur != nullptr)
        {
            // 达到发送时间：释放节点
            if(time >= cur->sendtime)
            {
                // 如果是尾节点，更新tail指针
                if(cur == tail)
                {
                    tail = prev;
                }
                prev->next = cur->next;
                freeNode(cur, dst_fd);  // 释放当前节点（子类重写后会先发送数据包）
                cur = prev->next;
            }
            else
            {
                // 未超时：由于数据包按时间排序，后续节点也不会超时，直接退出
                // TODO(bannos)：如果rtt是时变的，那么后续加入的数据包可能需要提前发送
                return;
            }
            
        }
    }
};

/**
 * @class TapInterface
 * @brief TAP虚拟网络接口管理类（继承链表类，实现流量控制）
 * @details 封装TAP接口的创建、桥接配置、epoll监听、数据包读写、流量控制（延迟/带宽/丢包）
 */
class TapInterface : public ListNode
{
public:
    // epoll事件结构体：当前事件 + 事件数组（存储epoll_wait返回的事件）
    struct epoll_event event, events[MAX_EVENTS];

    // --------------- 成员函数声明 ---------------
    void set_delay_ms(int64_t );          // 设置数据包延迟（毫秒）
    void set_bw(int64_t );                // 设置带宽限制（单位：bps）
    TapInterface(const char *, const char * ,const char *, int64_t, int64_t); // 构造函数
    ~TapInterface();                      // 析构函数
    int tap_open();                       // 创建并配置TAP接口
    int tap_close(int fd);                // 关闭TAP接口
    int tap_read();                       // 从TAP接口读取数据包（epoll监听）
    void tap_write();                     // 发送超时的数据包（释放节点）
    int64_t get_ms();                     // 获取当前时间戳（毫秒）
    int64_t get_us();                     // 获取当前时间戳（微秒）
    int get_tap();                        // 获取TAP接口fd
    void set_dstap(int fd);               // 设置目标TAP接口fd（跨接口转发）
    void set_loss(int loss);              // 设置丢包率（千分比）
    void printData(const unsigned char* data, size_t size); // 调试：打印数据包十六进制
    void freeNode(Node *node, int dst_fd)  override; // 重写释放节点（添加发送+丢包逻辑）
    bool chance_in_a_thousand(int chance); // 随机丢包判断（千分比概率）
    
    // 获取接口名
    std::string get_tap_name() const { return tap_name; }

private:
    std::string tap_name;   // TAP接口名（如tap0）
    std::string br_name;    // 桥接接口名（如aif）
    std::string eth_name;   // 物理网卡名（如eth2_h）
    int tap_fd;             // TAP接口文件描述符
    int dst_fd;             // 目标TAP接口fd（转发目标）
    int epoll_fd;           // epoll实例fd
    int64_t delay_ms;       // 数据包延迟时间（毫秒）
    int64_t bandwidth;      // 带宽限制（bps）
    int64_t pre_time;       // 上一个数据包的计划发送时间（微秒，用于带宽计算）
    int64_t packet_cnt;     // 接收数据包计数（用于统计）
    int Bloss;              // 丢包率（千分比，如10=1%丢包）
};

// 线程函数声明
void thread_function(TapInterface *tap);

#endif