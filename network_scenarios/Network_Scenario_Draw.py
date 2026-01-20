#!/usr/bin/env python3
"""
网络仿真场景可视化工具（图例内嵌版）- 修复版
绘制8个不同场景的带宽、时延和误码率变化曲线，图例放在每个子图内部
"""

import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import numpy as np
import os
from typing import Dict, List, Tuple, Optional
import matplotlib

# 设置中文字体（如果需要显示中文标签）
matplotlib.rcParams['font.sans-serif'] = ['SimHei', 'Arial Unicode MS', 'DejaVu Sans']
matplotlib.rcParams['axes.unicode_minus'] = False

class NetworkScenarioVisualizer:
    """网络场景可视化器"""
    
    def __init__(self, scenario_dir: str = "."):
        """
        初始化可视化器
        
        Args:
            scenario_dir: 场景脚本文件目录
        """
        self.scenario_dir = scenario_dir
        
        # 定义要可视化的场景文件
        self.scenario_files = {
            '低-中-低拥塞场景': 'scenario_low_medium_low.txt',
            '低-高-低拥塞场景': 'scenario_low_high_low.txt',
            '正常-高-正常场景': 'scenario_normal_high_normal.txt',
            '低-中-高拥塞场景': 'scenario_low_medium_high.txt',
            '中-高-中拥塞场景': 'scenario_medium_high_medium.txt',
            '逐步恶化场景': 'scenario_progressive_worse.txt',
            '逐步恢复场景': 'scenario_progressive_recovery.txt',
            '波动网络场景': 'scenario_fluctuating.txt'
        }
        
        # 颜色映射
        self.colors = {
            'bandwidth': '#1f77b4',    # 蓝色
            'delay': '#ff7f0e',        # 橙色
            'loss': '#2ca02c',         # 绿色
        }
        
        # 标记样式
        self.markers = {
            'bandwidth': 'o',    # 圆圈
            'delay': 's',        # 方形
            'loss': '^',         # 三角形
        }
        
        # 线型
        self.linestyles = {
            'bandwidth': '-',    # 实线
            'delay': '--',       # 虚线
            'loss': ':',         # 点线
        }
    
    def parse_scenario_file(self, filename: str) -> Optional[Dict]:
        """
        解析场景脚本文件
        
        Args:
            filename: 脚本文件名
            
        Returns:
            包含时间序列数据的字典
        """
        data = {
            'times': [],        # 时间点（秒）
            'bandwidths': [],   # 带宽（Mbps）
            'delays': [],       # 时延（ms）
            'losses': [],       # 丢包率（‰）
            'descriptions': [], # 描述
            'phases': [],       # 阶段标记
        }
        
        try:
            with open(filename, 'r', encoding='utf-8') as f:
                lines = f.readlines()
                
                for line in lines:
                    # 跳过注释行和空行
                    if line.strip() == '' or line.strip().startswith('#'):
                        continue
                    
                    # 解析数据行
                    parts = line.strip().split()
                    if len(parts) >= 6:
                        start_time_ms = int(parts[0])
                        duration_ms = int(parts[1])
                        bandwidth = int(parts[2])
                        delay = int(parts[3])
                        loss = int(parts[4])
                        description = ' '.join(parts[5:])
                        
                        # 将毫秒转换为秒
                        start_time_s = start_time_ms / 1000
                        duration_s = duration_ms / 1000
                        
                        # 使用时间段的中间点作为绘图点
                        mid_time = start_time_s + duration_s / 2
                        
                        data['times'].append(mid_time)
                        data['bandwidths'].append(bandwidth)
                        data['delays'].append(delay)
                        data['losses'].append(loss)
                        data['descriptions'].append(description)
                        
                        # 判断属于哪个阶段（0-400s, 400-800s, 800-1200s）
                        if start_time_s < 400:
                            data['phases'].append(1)
                        elif start_time_s < 800:
                            data['phases'].append(2)
                        else:
                            data['phases'].append(3)
        
        except FileNotFoundError:
            print(f"警告: 文件 {filename} 未找到")
            return None
        except Exception as e:
            print(f"解析文件 {filename} 时出错: {e}")
            return None
            
        return data
    
    def create_visualization(self, show_bandwidth: bool = True, 
                           show_delay: bool = True, 
                           show_loss: bool = True,
                           output_file: str = "network_scenarios_visualization.png"):
        """
        创建网络场景可视化（图例内嵌在子图中）
        
        Args:
            show_bandwidth: 是否显示带宽曲线
            show_delay: 是否显示时延曲线
            show_loss: 是否显示丢包率曲线
            output_file: 输出图像文件名
        """
        # 检查是否有要显示的数据
        if not (show_bandwidth or show_delay or show_loss):
            print("错误: 至少需要选择一个参数显示")
            return
        
        # 计算要显示的参数数量
        param_count = sum([show_bandwidth, show_delay, show_loss])
        
        # 创建图形 - 根据参数数量调整布局
        if param_count == 1:
            # 单个参数，所有图放在一行
            fig, axes = plt.subplots(2, 4, figsize=(20, 8))
            axes = axes.flatten()
        elif param_count == 2:
            # 两个参数，需要更多垂直空间
            fig, axes = plt.subplots(2, 4, figsize=(20, 10))
            axes = axes.flatten()
        else:  # param_count == 3
            # 三个参数，需要更多垂直空间
            fig = plt.figure(figsize=(20, 12))
            axes = []
            for i in range(8):
                ax = plt.subplot(4, 2, i+1)
                axes.append(ax)
        
        # 加载并绘制每个场景的数据
        for idx, (scenario_name, filename) in enumerate(self.scenario_files.items()):
            ax = axes[idx]
            
            # 解析场景数据
            data = self.parse_scenario_file(filename)
            if data is None:
                ax.text(0.5, 0.5, f"无法加载\n{scenario_name}", 
                       ha='center', va='center', transform=ax.transAxes)
                ax.set_title(scenario_name, fontsize=10, pad=5)
                continue
            
            times = data['times']
            lines = []  # 用于存储图例的线条
            labels = []  # 用于存储图例的标签
            
            # 创建多个y轴用于不同参数
            axes_list = [ax]  # 主轴
            ax2, ax3 = None, None  # 初始化额外的坐标轴
            
            # 根据显示的参数创建额外的y轴
            if show_delay and show_bandwidth:
                ax2 = ax.twinx()
                axes_list.append(ax2)
            
            if show_loss and (show_bandwidth or show_delay):
                if show_bandwidth and show_delay:
                    ax3 = ax.twinx()
                    ax3.spines['right'].set_position(('outward', 60))
                    axes_list.append(ax3)
                else:
                    # 只有两个参数，不需要偏移
                    if ax2 is None:  # 如果没有ax2，创建一个
                        ax2 = ax.twinx()
                        axes_list.append(ax2)
            
            # 绘制带宽曲线
            if show_bandwidth:
                line = ax.plot(times, data['bandwidths'], 
                               color=self.colors['bandwidth'], 
                               linestyle=self.linestyles['bandwidth'],
                               linewidth=2, 
                               marker=self.markers['bandwidth'], 
                               markersize=4,
                               markevery=5)[0]  # 每5个点显示一个标记
                lines.append(line)
                labels.append('带宽 (Mbps)')
                
                # 设置y轴标签
                ax.set_ylabel('带宽 (Mbps)', color=self.colors['bandwidth'], fontsize=10)
                ax.tick_params(axis='y', labelcolor=self.colors['bandwidth'])
            
            # 绘制时延曲线
            if show_delay:
                # 确定使用哪个坐标轴
                if show_bandwidth:
                    target_ax = ax2  # 带宽已显示，时延使用第二个y轴
                else:
                    target_ax = ax   # 带宽未显示，时延使用主轴
                
                line = target_ax.plot(times, data['delays'], 
                                     color=self.colors['delay'], 
                                     linestyle=self.linestyles['delay'],
                                     linewidth=2,
                                     marker=self.markers['delay'], 
                                     markersize=4,
                                     markevery=5)[0]
                lines.append(line)
                labels.append('时延 (ms)')
                
                # 设置y轴标签
                if target_ax != ax:
                    target_ax.set_ylabel('时延 (ms)', color=self.colors['delay'], fontsize=10)
                    target_ax.tick_params(axis='y', labelcolor=self.colors['delay'])
            
            # 绘制丢包率曲线
            if show_loss:
                # 确定使用哪个坐标轴
                if show_bandwidth and show_delay:
                    target_ax = ax3  # 三个参数都显示，丢包率使用第三个y轴
                elif show_bandwidth or show_delay:
                    # 显示两个参数，丢包率使用第二个y轴
                    if ax2 is not None:
                        target_ax = ax2
                    else:
                        # 创建第二个y轴
                        ax2 = ax.twinx()
                        target_ax = ax2
                else:
                    target_ax = ax   # 只显示丢包率，使用主轴
                
                line = target_ax.plot(times, data['losses'], 
                                     color=self.colors['loss'], 
                                     linestyle=self.linestyles['loss'],
                                     linewidth=2,
                                     marker=self.markers['loss'], 
                                     markersize=4,
                                     markevery=5)[0]
                lines.append(line)
                labels.append('丢包率 (‰)')
                
                # 设置y轴标签
                if target_ax != ax:
                    target_ax.set_ylabel('丢包率 (‰)', color=self.colors['loss'], fontsize=10)
                    target_ax.tick_params(axis='y', labelcolor=self.colors['loss'])
            
            # 设置子图标题和x轴标签
            ax.set_title(scenario_name, fontsize=12, pad=10, fontweight='bold')
            ax.set_xlabel('时间 (秒)', fontsize=10)
            
            # 设置x轴范围
            ax.set_xlim(0, 1200)
            
            # 添加网格
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # 添加阶段分隔线
            ax.axvline(x=400, color='gray', linestyle=':', alpha=0.5, linewidth=1)
            ax.axvline(x=800, color='gray', linestyle=':', alpha=0.5, linewidth=1)
            
            # 添加阶段标签
            y_pos = ax.get_ylim()[1] * 0.95
            ax.text(200, y_pos, '阶段1', 
                   ha='center', va='top', fontsize=8, alpha=0.7,
                   bbox=dict(boxstyle='round,pad=0.2', facecolor='yellow', alpha=0.2))
            ax.text(600, y_pos, '阶段2', 
                   ha='center', va='top', fontsize=8, alpha=0.7,
                   bbox=dict(boxstyle='round,pad=0.2', facecolor='lightblue', alpha=0.2))
            ax.text(1000, y_pos, '阶段3', 
                   ha='center', va='top', fontsize=8, alpha=0.7,
                   bbox=dict(boxstyle='round,pad=0.2', facecolor='lightgreen', alpha=0.2))
            
            # 添加图例到子图内部
            # 根据参数数量选择图例位置
            if param_count == 1:
                # 单个参数，图例放在右上角
                legend_loc = 'upper right'
            elif param_count == 2:
                # 两个参数，图例放在右上角
                legend_loc = 'upper right'
            else:  # param_count == 3
                # 三个参数，图例放在右上方，但避免遮挡重要数据
                legend_loc = 'upper right'
            
            # 创建图例
            legend = ax.legend(lines, labels, 
                              loc=legend_loc, 
                              fontsize=9,
                              frameon=True, 
                              fancybox=True, 
                              framealpha=0.8,
                              edgecolor='black')
            
            # 设置图例背景颜色
            legend.get_frame().set_facecolor('white')
            legend.get_frame().set_alpha(0.8)
        
        # 如果少于8个场景，隐藏多余的子图
        for idx in range(len(self.scenario_files), len(axes)):
            axes[idx].set_visible(False)
        
        # 添加整体标题
        title_text = '网络仿真场景可视化 (总时长: 1200秒)'
        if not show_bandwidth:
            title_text += ' - 不显示带宽'
        if not show_delay:
            title_text += ' - 不显示时延'
        if not show_loss:
            title_text += ' - 不显示丢包率'
        
        plt.suptitle(title_text, 
                    fontsize=16, fontweight='bold', y=0.98)
        
        # 调整布局
        plt.tight_layout(rect=[0, 0, 1, 0.96])
        
        # 保存图像
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"可视化图像已保存到: {output_file}")
        
        # 显示图像
        plt.show()
    
    def create_individual_plots(self, show_bandwidth: bool = True, 
                              show_delay: bool = True, 
                              show_loss: bool = True):
        """
        为每个场景创建单独的图像文件，每个图像包含完整图例
        
        Args:
            show_bandwidth: 是否显示带宽曲线
            show_delay: 是否显示时延曲线
            show_loss: 是否显示丢包率曲线
        """
        # 检查是否有要显示的数据
        if not (show_bandwidth or show_delay or show_loss):
            print("错误: 至少需要选择一个参数显示")
            return
        
        for scenario_name, filename in self.scenario_files.items():
            # 解析场景数据
            data = self.parse_scenario_file(filename)
            if data is None:
                print(f"跳过场景: {scenario_name} (无法加载数据)")
                continue
            
            # 创建图形
            fig, ax = plt.subplots(figsize=(14, 8))
            
            times = data['times']
            lines = []  # 用于存储图例的线条
            labels = []  # 用于存储图例的标签
            
            # 初始化额外的坐标轴
            ax2, ax3 = None, None
            
            # 绘制带宽曲线
            if show_bandwidth:
                line = ax.plot(times, data['bandwidths'], 
                               color=self.colors['bandwidth'], 
                               linestyle=self.linestyles['bandwidth'],
                               linewidth=2, 
                               marker=self.markers['bandwidth'], 
                               markersize=6,
                               markevery=5)[0]  # 每5个点显示一个标记
                lines.append(line)
                labels.append('带宽 (Mbps)')
                
                # 设置y轴标签
                ax.set_ylabel('带宽 (Mbps)', color=self.colors['bandwidth'], fontsize=12)
                ax.tick_params(axis='y', labelcolor=self.colors['bandwidth'])
            
            # 绘制时延曲线
            if show_delay:
                # 确定使用哪个坐标轴
                if show_bandwidth:
                    # 带宽已显示，时延使用第二个y轴
                    if ax2 is None:
                        ax2 = ax.twinx()
                    target_ax = ax2
                else:
                    # 带宽未显示，时延使用主轴
                    target_ax = ax
                
                line = target_ax.plot(times, data['delays'], 
                                     color=self.colors['delay'], 
                                     linestyle=self.linestyles['delay'],
                                     linewidth=2,
                                     marker=self.markers['delay'], 
                                     markersize=6,
                                     markevery=5)[0]
                lines.append(line)
                labels.append('时延 (ms)')
                
                # 设置y轴标签
                if target_ax != ax:
                    target_ax.set_ylabel('时延 (ms)', color=self.colors['delay'], fontsize=12)
                    target_ax.tick_params(axis='y', labelcolor=self.colors['delay'])
            
            # 绘制丢包率曲线
            if show_loss:
                # 确定使用哪个坐标轴
                if show_bandwidth and show_delay:
                    # 三个参数都显示，丢包率使用第三个y轴
                    if ax3 is None:
                        ax3 = ax.twinx()
                        ax3.spines['right'].set_position(('outward', 60))
                    target_ax = ax3
                elif show_bandwidth or show_delay:
                    # 显示两个参数，丢包率使用第二个y轴
                    if ax2 is None:
                        ax2 = ax.twinx()
                    target_ax = ax2
                else:
                    # 只显示丢包率，使用主轴
                    target_ax = ax
                
                line = target_ax.plot(times, data['losses'], 
                                     color=self.colors['loss'], 
                                     linestyle=self.linestyles['loss'],
                                     linewidth=2,
                                     marker=self.markers['loss'], 
                                     markersize=6,
                                     markevery=5)[0]
                lines.append(line)
                labels.append('丢包率 (‰)')
                
                # 设置y轴标签
                if target_ax != ax:
                    target_ax.set_ylabel('丢包率 (‰)', color=self.colors['loss'], fontsize=12)
                    target_ax.tick_params(axis='y', labelcolor=self.colors['loss'])
            
            # 设置标题和x轴标签
            title_text = f'{scenario_name} (总时长: 1200秒)'
            if not show_bandwidth:
                title_text += ' - 不显示带宽'
            if not show_delay:
                title_text += ' - 不显示时延'
            if not show_loss:
                title_text += ' - 不显示丢包率'
            
            ax.set_title(title_text, fontsize=16, fontweight='bold', pad=15)
            ax.set_xlabel('时间 (秒)', fontsize=12)
            
            # 设置x轴范围
            ax.set_xlim(0, 1200)
            
            # 添加网格
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # 添加阶段分隔线
            for x in [400, 800]:
                ax.axvline(x=x, color='gray', linestyle=':', alpha=0.7, linewidth=1)
            
            # 添加阶段标签
            y_max = ax.get_ylim()[1]
            ax.text(200, y_max * 0.95, '阶段1', 
                   ha='center', va='top', fontsize=11, alpha=0.8,
                   bbox=dict(boxstyle='round,pad=0.3', facecolor='yellow', alpha=0.3))
            ax.text(600, y_max * 0.95, '阶段2', 
                   ha='center', va='top', fontsize=11, alpha=0.8,
                   bbox=dict(boxstyle='round,pad=0.3', facecolor='lightblue', alpha=0.3))
            ax.text(1000, y_max * 0.95, '阶段3', 
                   ha='center', va='top', fontsize=11, alpha=0.8,
                   bbox=dict(boxstyle='round,pad=0.3', facecolor='lightgreen', alpha=0.3))
            
            # 添加图例
            # 将图例放在左上角，避免遮挡曲线
            legend = ax.legend(lines, labels, 
                              loc='upper left', 
                              fontsize=12,
                              frameon=True, 
                              fancybox=True, 
                              framealpha=0.9,
                              edgecolor='black',
                              shadow=True)
            
            # 设置图例背景颜色
            legend.get_frame().set_facecolor('white')
            
            # 调整布局
            plt.tight_layout()
            
            # 保存图像
            output_filename = f"scenario_{scenario_name.replace(' ', '_').replace('-', '_')}.png"
            plt.savefig(output_filename, dpi=150, bbox_inches='tight')
            print(f"场景图像已保存: {output_filename}")
            
            # 显示图像
            plt.show()
            plt.close(fig)  # 关闭图形，释放内存
    
    def create_comparison_plot(self, show_bandwidth: bool = True, 
                             show_delay: bool = True, 
                             show_loss: bool = True,
                             output_file: str = "scenarios_comparison.png"):
        """
        创建所有场景的对比图，将所有场景的同一参数放在同一图中比较
        
        Args:
            show_bandwidth: 是否显示带宽曲线
            show_delay: 是否显示时延曲线
            show_loss: 是否显示丢包率曲线
            output_file: 输出图像文件名
        """
        # 检查是否有要显示的数据
        if not (show_bandwidth or show_delay or show_loss):
            print("错误: 至少需要选择一个参数显示")
            return
        
        # 创建图形
        fig, axes = plt.subplots(show_bandwidth + show_delay + show_loss, 1, 
                                figsize=(16, 5 * (show_bandwidth + show_delay + show_loss)),
                                sharex=True)
        
        # 如果只有一个参数，将axes转换为列表
        if show_bandwidth + show_delay + show_loss == 1:
            axes = [axes]
        
        ax_idx = 0
        
        # 定义线型和颜色用于不同场景
        line_styles = ['-', '--', '-.', ':']
        colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', 
                 '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']
        
        # 收集所有场景的数据
        all_data = {}
        for scenario_name, filename in self.scenario_files.items():
            data = self.parse_scenario_file(filename)
            if data:
                all_data[scenario_name] = data
        
        # 绘制带宽对比
        if show_bandwidth:
            ax = axes[ax_idx]
            ax_idx += 1
            
            for i, (scenario_name, data) in enumerate(all_data.items()):
                line_style = line_styles[i % len(line_styles)]
                color = colors[i % len(colors)]
                
                ax.plot(data['times'], data['bandwidths'], 
                       label=scenario_name,
                       color=color,
                       linestyle=line_style,
                       linewidth=2)
            
            ax.set_ylabel('带宽 (Mbps)', fontsize=12)
            ax.set_title('带宽对比 - 所有场景', fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # 添加图例
            ax.legend(loc='upper right', fontsize=10, frameon=True, framealpha=0.8)
            
            # 添加阶段分隔线
            for x in [400, 800]:
                ax.axvline(x=x, color='gray', linestyle=':', alpha=0.5, linewidth=1)
        
        # 绘制时延对比
        if show_delay:
            ax = axes[ax_idx]
            ax_idx += 1
            
            for i, (scenario_name, data) in enumerate(all_data.items()):
                line_style = line_styles[i % len(line_styles)]
                color = colors[i % len(colors)]
                
                ax.plot(data['times'], data['delays'], 
                       label=scenario_name,
                       color=color,
                       linestyle=line_style,
                       linewidth=2)
            
            ax.set_ylabel('时延 (ms)', fontsize=12)
            ax.set_title('时延对比 - 所有场景', fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # 添加图例
            ax.legend(loc='upper right', fontsize=10, frameon=True, framealpha=0.8)
            
            # 添加阶段分隔线
            for x in [400, 800]:
                ax.axvline(x=x, color='gray', linestyle=':', alpha=0.5, linewidth=1)
        
        # 绘制丢包率对比
        if show_loss:
            ax = axes[ax_idx]
            
            for i, (scenario_name, data) in enumerate(all_data.items()):
                line_style = line_styles[i % len(line_styles)]
                color = colors[i % len(colors)]
                
                ax.plot(data['times'], data['losses'], 
                       label=scenario_name,
                       color=color,
                       linestyle=line_style,
                       linewidth=2)
            
            ax.set_ylabel('丢包率 (‰)', fontsize=12)
            ax.set_xlabel('时间 (秒)', fontsize=12)
            ax.set_title('丢包率对比 - 所有场景', fontsize=14, fontweight='bold')
            ax.grid(True, alpha=0.3, linestyle='--')
            
            # 添加图例
            ax.legend(loc='upper right', fontsize=10, frameon=True, framealpha=0.8)
            
            # 添加阶段分隔线
            for x in [400, 800]:
                ax.axvline(x=x, color='gray', linestyle=':', alpha=0.5, linewidth=1)
        
        # 设置x轴范围
        axes[-1].set_xlim(0, 1200)
        
        # 调整布局
        plt.tight_layout()
        
        # 保存图像
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        print(f"对比图像已保存到: {output_file}")
        
        # 显示图像
        plt.show()

def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='网络仿真场景可视化工具（图例内嵌版）- 修复版')
    parser.add_argument('--mode', choices=['all', 'individual', 'comparison'], default='all',
                       help='可视化模式: all(所有场景在一张图), individual(每个场景单独图), comparison(参数对比图)')
    parser.add_argument('--bw', action='store_true', default=True,
                       help='显示带宽曲线 (默认: 显示)')
    parser.add_argument('--no-bw', action='store_false', dest='bw',
                       help='不显示带宽曲线')
    parser.add_argument('--delay', action='store_true', default=True,
                       help='显示时延曲线 (默认: 显示)')
    parser.add_argument('--no-delay', action='store_false', dest='delay',
                       help='不显示时延曲线')
    parser.add_argument('--loss', action='store_true', default=True,
                       help='显示丢包率曲线 (默认: 显示)')
    parser.add_argument('--no-loss', action='store_false', dest='loss',
                       help='不显示丢包率曲线')
    parser.add_argument('--output', type=str, default='network_scenarios.png',
                       help='输出图像文件名')
    parser.add_argument('--dir', type=str, default='.',
                       help='场景文件目录')
    
    args = parser.parse_args()
    
    # 创建可视化器
    visualizer = NetworkScenarioVisualizer(args.dir)
    
    # 检查场景文件是否存在
    print("检查场景文件...")
    missing_files = []
    for scenario_name, filename in visualizer.scenario_files.items():
        if not os.path.exists(filename):
            missing_files.append((scenario_name, filename))
    
    if missing_files:
        print("\n⚠️  警告: 以下场景文件未找到:")
        for scenario_name, filename in missing_files:
            print(f"  {scenario_name}: {filename}")
        print("\n请确保已经生成了场景文件或使用正确的目录(--dir选项)")
        response = input("是否继续? (y/n): ")
        if response.lower() != 'y':
            return
    
    print(f"显示设置: 带宽={args.bw}, 时延={args.delay}, 丢包率={args.loss}")
    
    if args.mode == 'all':
        print("创建所有场景的可视化（一张大图）...")
        visualizer.create_visualization(
            show_bandwidth=args.bw,
            show_delay=args.delay,
            show_loss=args.loss,
            output_file=args.output
        )
        
    elif args.mode == 'individual':
        print("为每个场景创建单独的图像文件...")
        visualizer.create_individual_plots(
            show_bandwidth=args.bw,
            show_delay=args.delay,
            show_loss=args.loss
        )
        
    elif args.mode == 'comparison':
        print("创建场景对比图...")
        visualizer.create_comparison_plot(
            show_bandwidth=args.bw,
            show_delay=args.delay,
            show_loss=args.loss,
            output_file=args.output
        )

if __name__ == "__main__":
    main()