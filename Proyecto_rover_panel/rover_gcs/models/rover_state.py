import time
from dataclasses import dataclass, field
from typing import List
from PyQt6.QtCore import QObject, pyqtSignal

@dataclass
class TelemetryData:
    battery_voltage: float = 0.0
    motor_speeds: List[float] = field(default_factory=lambda: [0.0] * 6)
    stepper_position: int = 0
    stepper_moving: bool = False
    uptime_seconds: int = 0
    rssi: int = -100
    last_update_time: float = 0.0

@dataclass
class CommandData:
    linear_speed: float = 0.0       # -100.0 to +100.0
    angular_speed: float = 0.0      # -100.0 to +100.0
    servo_angles: List[float] = field(default_factory=lambda: [90.0] * 16)
    stepper_target: int = 0
    stepper_speed: int = 1000
    emergency_stop: bool = False

class RoverStateModel(QObject):
    """
    Central Thread-Safe Data Model for 6x6 Rover State in GCS.
    Emits PyQt Signals when telemetry or connection state updates.
    """
    telemetry_updated = pyqtSignal(TelemetryData)
    connection_status_changed = pyqtSignal(bool, str)

    def __init__(self):
        super().__init__()
        self.telemetry = TelemetryData()
        self.command = CommandData()
        self.is_connected = False
        self.rover_ip = "192.168.4.1"
        self.command_port = 5005
        self.telemetry_port = 5006

    def update_telemetry(self, battery_voltage: float, motor_speeds: List[float],
                         stepper_pos: int, stepper_moving: bool, uptime: int, rssi: int):
        self.telemetry.battery_voltage = battery_voltage
        self.telemetry.motor_speeds = motor_speeds
        self.telemetry.stepper_position = stepper_pos
        self.telemetry.stepper_moving = stepper_moving
        self.telemetry.uptime_seconds = uptime
        self.telemetry.rssi = rssi
        self.telemetry.last_update_time = time.time()

        if not self.is_connected:
            self.is_connected = True
            self.connection_status_changed.emit(True, f"Connected to Rover ({self.rover_ip})")

        self.telemetry_updated.emit(self.telemetry)

    def set_disconnected(self):
        if self.is_connected:
            self.is_connected = False
            self.connection_status_changed.emit(False, "Disconnected / Connection Lost")

    def set_drive_command(self, linear: float, angular: float):
        self.command.linear_speed = max(-100.0, min(100.0, linear))
        self.command.angular_speed = max(-100.0, min(100.0, angular))

    def set_servo_angle(self, channel: int, angle: float):
        if 0 <= channel < 16:
            self.command.servo_angles[channel] = max(0.0, min(180.0, angle))

    def set_stepper_command(self, target: int, speed_hz: int = 1000):
        self.command.stepper_target = target
        self.command.stepper_speed = speed_hz

    def set_emergency_stop(self, active: bool):
        self.command.emergency_stop = active
        if active:
            self.command.linear_speed = 0.0
            self.command.angular_speed = 0.0
