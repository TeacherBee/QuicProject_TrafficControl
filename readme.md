### introduction
本代码用于模拟网络链路状态的带宽、时延、丢包率三个特性

### use
# 编译
g++ -std=c++14 -pthread -o tc_quic tc_quic.cc

# 1. 运行内置演示脚本（总时长40秒）
sudo ./tc_quic --total_time=40000 --demo

# 2. 运行自定义脚本文件（总时长45秒）
sudo ./tc_quic --total_time=45000 --script=network_scenario.txt

# 3. 运行简单测试（总时长30秒）
sudo ./tc_quic --total_time=30000

# 4. 交互模式（传统方式）
sudo ./tc_quic