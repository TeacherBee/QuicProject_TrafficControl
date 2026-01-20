#!/usr/bin/env python3
"""
网络仿真脚本生成器
生成包含不同拥塞程度组合的1200秒仿真脚本
"""

import random
import argparse
from typing import List, Tuple, Dict

class NetworkScenarioGenerator:
    """网络场景生成器"""
    
    # 拥塞程度定义
    CONGESTION_LEVELS = {
        'low': {
            'name': '低拥塞',
            'bandwidth_range': (90, 100),    # Mbps
            'delay_range': (100, 200),         # ms
            'loss_range': (1, 10),            # ‰
            'fluctuation': 0.1               # 波动幅度百分比
        },
        'medium': {
            'name': '中拥塞', 
            'bandwidth_range': (80, 90),     # Mbps
            'delay_range': (200, 400),        # ms
            'loss_range': (10, 50),          # ‰
            'fluctuation': 0.15              # 波动幅度百分比
        },
        'high': {
            'name': '高拥塞',
            'bandwidth_range': (70, 80),      # Mbps
            'delay_range': (400, 600),       # ms
            'loss_range': (50, 200),         # ‰
            'fluctuation': 0.2               # 波动幅度百分比
        },
        'normal': {
            'name': '正常网络',
            'bandwidth_range': (100, 120),    # Mbps
            'delay_range': (50, 100),         # ms
            'loss_range': (0, 1),            # ‰
            'fluctuation': 0.05              # 波动幅度百分比
        }
    }
    
    def __init__(self, total_duration_ms: int = 1200000):
        """
        初始化生成器
        
        Args:
            total_duration_ms: 总仿真时间（毫秒），默认1200000ms=1200s
        """
        self.total_duration_ms = total_duration_ms
        self.phase_duration_ms = total_duration_ms // 3  # 每个阶段400秒
        
    def generate_random_value(self, base_value: float, min_range: float, 
                            max_range: float, fluctuation: float) -> float:
        """
        在基准值附近生成随机值，模拟小幅波动
        
        Args:
            base_value: 基准值
            min_range: 最小值
            max_range: 最大值
            fluctuation: 波动幅度百分比
            
        Returns:
            随机生成的值
        """
        # 计算波动范围
        fluctuation_amount = base_value * fluctuation
        
        # 生成随机波动
        random_fluctuation = random.uniform(-fluctuation_amount, fluctuation_amount)
        
        # 应用波动
        new_value = base_value + random_fluctuation
        
        # 确保在允许范围内
        new_value = max(min_range, min(new_value, max_range))
        
        return new_value
    
    def generate_phase(self, phase_name: str, phase_type: str, 
                      start_time_ms: int, duration_ms: int) -> List[Tuple]:
        """
        生成一个“阶段”的网络事件
        
        Args:
            phase_name: 阶段名称
            phase_type: 拥塞程度类型（'low', 'medium', 'high', 'normal'）
            start_time_ms: 阶段开始时间（毫秒）
            duration_ms: 阶段持续时间（毫秒）
            
        Returns:
            该阶段的事件列表
        """
        events = []
        phase_config = self.CONGESTION_LEVELS[phase_type]
        
        # 阶段内分为多个小时间段，每个时间段有小幅波动
        segment_duration_ms = 10000  # 每10秒一个时间段
        num_segments = duration_ms // segment_duration_ms
        
        # 为该阶段生成基准值
        base_bandwidth = random.uniform(*phase_config['bandwidth_range'])
        base_delay = random.uniform(*phase_config['delay_range'])
        base_loss = random.uniform(*phase_config['loss_range'])
        
        for i in range(num_segments):
            # 每个时间段有轻微的参数变化
            segment_start = start_time_ms + i * segment_duration_ms
            
            # 生成带波动的参数
            bandwidth = self.generate_random_value(
                base_bandwidth, 
                *phase_config['bandwidth_range'][:2],  # 解包为两个参数
                phase_config['fluctuation']
            )
            
            delay = self.generate_random_value(
                base_delay,
                *phase_config['delay_range'][:2],
                phase_config['fluctuation']
            )
            
            loss = self.generate_random_value(
                base_loss,
                *phase_config['loss_range'][:2],
                phase_config['fluctuation']
            )
            
            # 确保值为整数
            bandwidth = int(bandwidth)
            delay = int(delay)
            loss = int(loss)
            
            # 创建事件
            description = f"{phase_name}: {phase_config['name']}-时间段{i+1}"
            events.append((segment_start, segment_duration_ms, bandwidth, delay, loss, description))
        
        return events
    
    def generate_scenario(self, phases: List[str], scenario_name: str) -> List[Tuple]:
        """
        生成完整场景
        
        Args:
            phases: 三个阶段拥塞程度的列表，如 ['low', 'medium', 'low']
            scenario_name: 场景名称
            
        Returns:
            整个场景的事件列表
        """
        if len(phases) != 3:
            raise ValueError("必须指定三个阶段")
        
        all_events = []
        
        for i, phase_type in enumerate(phases):
            if phase_type not in self.CONGESTION_LEVELS:
                raise ValueError(f"无效的拥塞程度: {phase_type}")
            
            phase_name = f"阶段{i+1}"
            phase_start = i * self.phase_duration_ms
            
            phase_events = self.generate_phase(
                phase_name, phase_type, phase_start, self.phase_duration_ms
            )
            
            all_events.extend(phase_events)
        
        return all_events
    
    def save_script(self, events: List[Tuple], filename: str, scenario_name: str):
        """
        保存脚本到文件
        
        Args:
            events: 事件列表
            filename: 输出文件名
            scenario_name: 场景名称
        """
        with open(filename, 'w', encoding='utf-8') as f:
            # 写入文件头
            f.write(f"# 网络仿真脚本 - {scenario_name}\n")
            f.write("# 总时长: 1200秒 (1200000ms)\n")
            f.write("# 格式: 开始时间(ms) 持续时间(ms) 带宽(Mbps) 延迟(ms) 丢包率(‰) 描述\n")
            f.write("#\n")
            
            # 写入事件
            for event in events:
                start_time, duration, bandwidth, delay, loss, desc = event
                f.write(f"{start_time} {duration} {bandwidth} {delay} {loss} {desc}\n")
        
        print(f"脚本已保存到: {filename}")
        print(f"事件数量: {len(events)}")
        
    def print_statistics(self, events: List[Tuple], scenario_name: str):
        """
        打印场景统计信息
        
        Args:
            events: 事件列表
            scenario_name: 场景名称
        """
        print(f"\n=== {scenario_name} 统计信息 ===")
        
        # 按阶段统计
        for phase_idx in range(3):
            phase_start = phase_idx * self.phase_duration_ms
            phase_end = (phase_idx + 1) * self.phase_duration_ms
            
            phase_events = [e for e in events if phase_start <= e[0] < phase_end]
            
            if phase_events:
                avg_bandwidth = sum(e[2] for e in phase_events) / len(phase_events)
                avg_delay = sum(e[3] for e in phase_events) / len(phase_events)
                avg_loss = sum(e[4] for e in phase_events) / len(phase_events)
                
                print(f"阶段 {phase_idx + 1}:")
                print(f"  平均带宽: {avg_bandwidth:.1f} Mbps")
                print(f"  平均延迟: {avg_delay:.1f} ms")
                print(f"  平均丢包: {avg_loss:.1f} ‰")
                print(f"  事件数量: {len(phase_events)}")

def generate_preset_scenarios(generator: NetworkScenarioGenerator):
    """生成预设的场景"""
    
    scenarios = [
        {
            'name': '低-中-低拥塞场景',
            'phases': ['low', 'medium', 'low'],
            'filename': 'scenario_low_medium_low.txt'
        },
        {
            'name': '低-高-低拥塞场景',
            'phases': ['low', 'high', 'low'],
            'filename': 'scenario_low_high_low.txt'
        },
        {
            'name': '正常-高-正常场景',
            'phases': ['normal', 'high', 'normal'],
            'filename': 'scenario_normal_high_normal.txt'
        },
        {
            'name': '低-中-高拥塞场景',
            'phases': ['low', 'medium', 'high'],
            'filename': 'scenario_low_medium_high.txt'
        },
        {
            'name': '中-高-中拥塞场景',
            'phases': ['medium', 'high', 'medium'],
            'filename': 'scenario_medium_high_medium.txt'
        },
        {
            'name': '逐步恶化场景',
            'phases': ['low', 'medium', 'high'],
            'filename': 'scenario_progressive_worse.txt'
        },
        {
            'name': '逐步恢复场景',
            'phases': ['high', 'medium', 'low'],
            'filename': 'scenario_progressive_recovery.txt'
        },
        {
            'name': '波动网络场景',
            'phases': ['low', 'high', 'medium'],
            'filename': 'scenario_fluctuating.txt'
        }
    ]
    
    all_scripts = []
    
    for scenario in scenarios:
        print(f"\n生成场景: {scenario['name']}")
        print(f"拥塞组合: {' -> '.join(scenario['phases'])}")
        
        try:
            events = generator.generate_scenario(scenario['phases'], scenario['name'])
            generator.save_script(events, scenario['filename'], scenario['name'])
            generator.print_statistics(events, scenario['name'])
            
            all_scripts.append({
                'name': scenario['name'],
                'filename': scenario['filename'],
                'events': events
            })
            
        except Exception as e:
            print(f"生成场景失败: {e}")
    
    return all_scripts

def generate_custom_scenario(generator: NetworkScenarioGenerator, phases: List[str], 
                           output_file: str, scenario_name: str):
    """生成自定义场景"""
    
    print(f"\n生成自定义场景: {scenario_name}")
    print(f"拥塞组合: {' -> '.join(phases)}")
    
    try:
        events = generator.generate_scenario(phases, scenario_name)
        generator.save_script(events, output_file, scenario_name)
        generator.print_statistics(events, scenario_name)
        
        return events
    except Exception as e:
        print(f"生成自定义场景失败: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(description='网络仿真脚本生成器')
    parser.add_argument('--mode', choices=['preset', 'custom'], default='preset',
                       help='生成模式: preset(预设场景) 或 custom(自定义场景)')
    parser.add_argument('--phases', type=str, nargs=3,
                       help='自定义三个阶段的拥塞程度: low medium high normal')
    parser.add_argument('--output', type=str, default='custom_scenario.txt',
                       help='自定义场景的输出文件名')
    parser.add_argument('--name', type=str, default='自定义场景',
                       help='自定义场景的名称')
    parser.add_argument('--seed', type=int, default=None,
                       help='随机种子（用于可重复的结果）')
    
    args = parser.parse_args()
    
    # 设置随机种子（如果指定）
    if args.seed is not None:
        random.seed(args.seed)
        print(f"使用随机种子: {args.seed}")
    
    # 创建生成器
    generator = NetworkScenarioGenerator(total_duration_ms=1200000)
    
    if args.mode == 'preset':
        print("生成预设的网络仿真场景...")
        print("总时长: 1200秒 (1200000ms)")
        print("每个阶段: 400秒 (400000ms)")
        print("每个小时间段: 10秒 (10000ms)")
        print("-" * 50)
        
        generate_preset_scenarios(generator)
        
        print("\n" + "=" * 50)
        print("所有预设场景已生成完成！")
        print("你可以使用以下命令运行仿真:")
        print("  ./tc_quic --total_time=1200000 --script=scenario_low_medium_low.txt")
        
    elif args.mode == 'custom':
        if not args.phases:
            print("错误: 自定义模式需要指定三个阶段")
            print("示例: --phases low medium high")
            return
        
        print(f"生成自定义网络仿真场景: {args.name}")
        print(f"总时长: 1200秒 (1200000ms)")
        print(f"三个阶段: {' -> '.join(args.phases)}")
        print("-" * 50)
        
        events = generate_custom_scenario(generator, args.phases, args.output, args.name)
        
        if events:
            print(f"\n自定义场景已保存到: {args.output}")
            print(f"使用方法: ./tc_quic --total_time=1200000 --script={args.output}")

if __name__ == "__main__":
    main()