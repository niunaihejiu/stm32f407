import socket
import time
import csv
from datetime import datetime

# ====================== 服务器配置 ======================
HOST = '0.0.0.0'  # 监听所有可用的网络接口（局域网内任意设备可连接）
PORT = 8080  # 端口号，必须和ESP8266代码里的TCP端口一致（AT+CIPSTART里的8080）
CSV_FILE = 'mq2_sensor_data.csv'  # 保存数据的CSV文件


# ====================== 初始化CSV文件（添加表头） ======================
def init_csv_file():
    """初始化CSV文件，写入表头（仅首次运行时）"""
    try:
        # 检查文件是否存在，不存在则创建并写表头
        with open(CSV_FILE, 'r', encoding='utf-8') as f:
            pass
    except FileNotFoundError:
        with open(CSV_FILE, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            # 表头：时间戳、ADC值、电压、DO状态、是否报警
            writer.writerow(['Timestamp', 'MQ2_ADC', 'MQ2_VOLT(V)', 'MQ2_DO', 'GAS_ALARM'])


# ====================== 解析传感器数据 ======================
def parse_sensor_data(data_str):
    """解析ESP-01S发送的字符串数据，返回结构化字典"""
    parsed_data = {
        'timestamp': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
        'mq2_adc': 'N/A',
        'mq2_volt': 'N/A',
        'mq2_do': 'N/A',
        'gas_alarm': 'N/A'
    }
    try:
        # 数据格式示例：MQ2_ADC:55,MQ2_VOLT:0.40V,MQ2_DO:1,GAS_ALARM:NO\r\n
        # 分割数据项
        data_items = data_str.strip().split(',')
        for item in data_items:
            key_value = item.split(':')
            if len(key_value) != 2:
                continue
            key, value = key_value[0].strip(), key_value[1].strip()
            # 解析各字段
            if key == 'MQ2_ADC':
                parsed_data['mq2_adc'] = value
            elif key == 'MQ2_VOLT':
                parsed_data['mq2_volt'] = value.replace('V', '')  # 去掉V单位，只保留数值
            elif key == 'MQ2_DO':
                parsed_data['mq2_do'] = value
            elif key == 'GAS_ALARM':
                parsed_data['gas_alarm'] = value
    except Exception as e:
        print(f"[解析数据失败] 错误：{e} | 原始数据：{data_str}")
    return parsed_data


# ====================== 保存数据到CSV ======================
def save_to_csv(data_dict):
    """将解析后的传感器数据保存到CSV文件"""
    try:
        with open(CSV_FILE, 'a', newline='', encoding='utf-8') as f:
            writer = csv.writer(f)
            # 按表头顺序写入数据
            writer.writerow([
                data_dict['timestamp'],
                data_dict['mq2_adc'],
                data_dict['mq2_volt'],
                data_dict['mq2_do'],
                data_dict['gas_alarm']
            ])
    except Exception as e:
        print(f"[保存CSV失败] 错误：{e}")


# ====================== 主服务器逻辑 ======================
def main():
    # 初始化CSV文件
    init_csv_file()

    # 创建TCP套接字
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server_socket:
        # 设置端口复用（避免服务器重启后端口被占用）
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # 绑定IP和端口
        server_socket.bind((HOST, PORT))
        # 开始监听连接（最大等待连接数5）
        server_socket.listen(5)
        print(f"======================")
        print(f"MQ2传感器数据服务器已启动")
        print(f"监听地址：{HOST}:{PORT}")
        print(f"数据保存文件：{CSV_FILE}")
        print(f"======================")
        print(f"等待ESP-01S连接...\n")

        while True:
            # 等待客户端连接（阻塞式）
            client_conn, client_addr = server_socket.accept()
            print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] 客户端已连接：{client_addr}")

            # 处理客户端数据
            with client_conn:
                # 设置接收缓冲区大小，适配传感器数据长度
                client_conn.settimeout(30)  # 30秒无数据则断开连接
                while True:
                    try:
                        # 接收数据（单次最多1024字节）
                        data = client_conn.recv(1024)
                        if not data:
                            # 客户端断开连接
                            print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] 客户端断开连接：{client_addr}\n")
                            break

                        # 解码字节数据为字符串（忽略无法解码的字符）
                        data_str = data.decode('utf-8', errors='ignore')
                        print(f"[接收数据] {data_str.strip()}")

                        # 解析并保存数据
                        parsed_data = parse_sensor_data(data_str)
                        save_to_csv(parsed_data)

                    except socket.timeout:
                        # 超时无数据，断开连接
                        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] 超时无数据，断开客户端：{client_addr}\n")
                        break
                    except Exception as e:
                        # 其他异常（如客户端强制断开）
                        print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] 通信异常：{e} | 客户端：{client_addr}\n")
                        break


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n[服务器停止] 用户手动终止程序")
    except Exception as e:
        print(f"\n[服务器异常] 错误：{e}")