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

### other file
## /network_scenarios:
# scenario_xxx.txt
网络仿真脚本，实现对tc的链路状态自动控制

# Network_Scenario_Generator.py
网络仿真脚本生成器，生成包含不同拥塞程度组合的1200秒仿真脚本（Network_Scenario_xxx.txt）

-使用方法：
--1.生成所有预设场景：python Network_Scenario_Generator.py
--2.生成自定义场景：# 示例：生成低->中->高拥塞场景
---python Network_Scenario_Generator.py --mode custom --phases low medium high --output my_scenario.txt --name "我的自定义场景"
--3.生成特定场景（带随机种子）：python Network_Scenario_Generator.py --mode custom --phases low high low --output test_scenario.txt --name "测试场景" --seed 12345

-预设场景包括：
--1.低-中-低拥塞场景：模拟网络临时拥堵后恢复
--2.低-高-低拥塞场景：模拟网络严重拥塞后恢复
--3.正常-高-正常场景：模拟网络故障后的完全恢复
--4.低-中-高拥塞场景：模拟网络逐渐恶化
--5.中-高-中拥塞场景：模拟中度网络条件下的拥塞
--6.逐步恶化场景：模拟网络性能持续下降
--7.逐步恢复场景：模拟网络从拥塞中逐渐恢复
--8.波动网络场景：模拟不稳定的网络条件

-输出文件格式：
    # 网络仿真脚本 - 低-中-低拥塞场景
    # 总时长: 1200秒 (1200000ms)
    # 格式: 开始时间(ms) 持续时间(ms) 带宽(Mbps) 延迟(ms) 丢包率(‰) 描述
    0 10000 95 35 2 阶段1: 低拥塞-时间段1
    10000 10000 92 38 1 阶段1: 低拥塞-时间段2
    ...

# Network_Scenario_Draw.py
读取网络仿真脚本，并作图

-使用方法1：所有场景在一张大图中（每个子图有自己的图例）
--1.显示所有参数：python Network_Scenario_Draw.py
--2.只显示时延和误码：python Network_Scenario_Draw.py --no-bw
--3.只显示时延：python Network_Scenario_Draw.py --no-bw --no-loss
--4.指定输出文件名：python Network_Scenario_Draw.py --output my_network_plots.png

-使用方法2：每个场景生成独立的图像文件（每个文件有完整图例）
--1.为每个场景生成独立的图像：python Network_Scenario_Draw.py --mode individual
--2.只显示时延和误码：python Network_Scenario_Draw.py --mode individual --no-bw

-使用方法3：创建对比图（所有场景的同一参数在同一图中比较）
--1.对比所有场景的带宽：python Network_Scenario_Draw.py --mode comparison
--2.对比所有场景的时延和误码：python Network_Scenario_Draw.py --mode comparison --no-bw

