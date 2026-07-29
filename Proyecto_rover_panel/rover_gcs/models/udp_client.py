import socket
import struct
import time
from PyQt6.QtCore import QThread, pyqtSignal
from models.rover_state import RoverStateModel

CMD_MAGIC = 0x5256   # 'RV'
TELEM_MAGIC = 0x544D # 'TM'

# Struct formats (Little endian, packed)
CMD_STRUCT_FMT = "<H I f f 16f i I B 3s"     # 90 bytes
TELEM_STRUCT_FMT = "<H I f 6f i B I b 2s"   # 46 bytes

class UDPCommThread(QThread):
    """
    Background QThread handling high-speed UDP telemetry reception & command transmission.
    """
    connection_timeout_signal = pyqtSignal()

    def __init__(self, model: RoverStateModel):
        super().__init__()
        self.model = model
        self.running = True
        self.sequence_tx = 0

        self.tx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx_socket.settimeout(0.1)

    def run(self):
        # Bind RX socket for telemetry broadcasts
        try:
            self.rx_socket.bind(("0.0.0.0", self.model.telemetry_port))
        except Exception as e:
            print(f"[UDP Comm] Warning binding telemetry port: {e}")

        last_tx_time = 0
        last_rx_time = time.time()

        while self.running:
            now = time.time()

            # 1. Transmit Command Packet at 20 Hz (50ms interval)
            if now - last_tx_time >= 0.05:
                self._send_command_packet()
                last_tx_time = now

            # 2. Receive Telemetry Packet
            try:
                data, addr = self.rx_socket.recvfrom(256)
                if len(data) == struct.calcsize(TELEM_STRUCT_FMT):
                    unpacked = struct.unpack(TELEM_STRUCT_FMT, data)
                    magic = unpacked[0]
                    if magic == TELEM_MAGIC:
                        last_rx_time = now
                        batt_v = unpacked[2]
                        motor_speeds = list(unpacked[3:9])
                        stepper_pos = unpacked[9]
                        stepper_mov = bool(unpacked[10])
                        uptime = unpacked[11]
                        rssi = unpacked[12]

                        self.model.update_telemetry(
                            battery_voltage=batt_v,
                            motor_speeds=motor_speeds,
                            stepper_pos=stepper_pos,
                            stepper_moving=stepper_mov,
                            uptime=uptime,
                            rssi=rssi
                        )
            except socket.timeout:
                pass
            except Exception as e:
                print(f"[UDP Comm] RX Exception: {e}")

            # 3. Connection Timeout Check (2.0 sec)
            if now - last_rx_time > 2.0:
                self.model.set_disconnected()

            time.sleep(0.005)

    def _send_command_packet(self):
        self.sequence_tx += 1
        cmd = self.model.command

        try:
            packed_pkt = struct.pack(
                CMD_STRUCT_FMT,
                CMD_MAGIC,
                self.sequence_tx,
                cmd.linear_speed,
                cmd.angular_speed,
                *cmd.servo_angles,
                cmd.stepper_target,
                cmd.stepper_speed,
                1 if cmd.emergency_stop else 0,
                b'\x00\x00\x00'
            )
            self.tx_socket.sendto(packed_pkt, (self.model.rover_ip, self.model.command_port))
        except Exception as e:
            print(f"[UDP Comm] TX Error: {e}")

    def stop(self):
        self.running = False
        self.wait()
        self.tx_socket.close()
        self.rx_socket.close()
