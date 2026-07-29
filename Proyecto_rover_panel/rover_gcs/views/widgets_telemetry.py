from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QLabel, QProgressBar, QTextEdit, QGridLayout
)
from PyQt6.QtCore import Qt, pyqtSlot
from models.rover_state import TelemetryData

class BatteryGaugeWidget(QWidget):
    """Visual Battery Gauge displaying Voltage and percentage level for 3S 12V LiPo."""
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)

        self.lbl_title = QLabel("Battery Level (12V 3S LiPo)")
        self.lbl_title.setStyleSheet("color: #94a3b8; font-weight: bold;")

        self.lbl_voltage = QLabel("0.00 V")
        self.lbl_voltage.setStyleSheet("font-size: 22px; font-weight: bold; color: #10b981;")

        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setTextVisible(True)
        self.progress_bar.setFixedHeight(22)

        layout.addWidget(self.lbl_title)
        layout.addWidget(self.lbl_voltage)
        layout.addWidget(self.progress_bar)
        self.setLayout(layout)

    def update_voltage(self, voltage: float):
        self.lbl_voltage.setText(f"{voltage:.2f} V")
        # 3S LiPo Range: 9.9V (0%) to 12.6V (100%)
        percentage = int(max(0.0, min(100.0, ((voltage - 9.9) / (12.6 - 9.9)) * 100.0)))
        self.progress_bar.setValue(percentage)

        # Color warning thresholds
        if voltage >= 11.4:
            color = "#10b981" # Green
        elif voltage >= 10.5:
            color = "#f59e0b" # Yellow / Orange
        else:
            color = "#ef4444" # Red Critical

        self.lbl_voltage.setStyleSheet(f"font-size: 22px; font-weight: bold; color: {color};")
        self.progress_bar.setStyleSheet(f"QProgressBar::chunk {{ background-color: {color}; }}")

class MotorTelemetryWidget(QWidget):
    """Displays 6 DC Motor Duty cycles (FL, FR, ML, MR, RL, RR)."""
    def __init__(self):
        super().__init__()
        layout = QGridLayout()
        layout.setSpacing(10)

        self.motor_labels = []
        self.motor_bars = []

        names = ["FL (M1)", "FR (M2)", "ML (M3)", "MR (M4)", "RL (M5)", "RR (M6)"]
        for i, name in enumerate(names):
            row = i // 2
            col = (i % 2) * 2

            lbl = QLabel(name)
            lbl.setStyleSheet("font-weight: bold; color: #38bdf8;")

            val_lbl = QLabel("0.0%")
            val_lbl.setAlignment(Qt.AlignmentFlag.AlignRight)

            bar = QProgressBar()
            bar.setRange(-100, 100)
            bar.setValue(0)
            bar.setFixedHeight(14)
            bar.setTextVisible(False)

            layout.addWidget(lbl, row, col)
            layout.addWidget(val_lbl, row, col + 1)
            layout.addWidget(bar, row + 1, col, 1, 2)

            self.motor_labels.append(val_lbl)
            self.motor_bars.append(bar)

        self.setLayout(layout)

    def update_motors(self, speeds: list):
        for i in range(min(6, len(speeds))):
            val = int(speeds[i])
            self.motor_labels[i].setText(f"{val:+d}%")
            self.motor_bars[i].setValue(val)

class TelemetryDashboardWidget(QWidget):
    """Main Telemetry view component aggregating telemetry metrics."""
    def __init__(self):
        super().__init__()
        main_layout = QVBoxLayout()

        # Telemetry Group Box
        group = QGroupBox("LIVE TELEMETRY & SENSORS")
        group_layout = QVBoxLayout()

        # Battery Gauge
        self.battery_widget = BatteryGaugeWidget()
        group_layout.addWidget(self.battery_widget)

        group_layout.addSpacing(10)

        # Motors Widget
        self.motors_widget = MotorTelemetryWidget()
        group_layout.addWidget(self.motors_widget)

        group_layout.addSpacing(10)

        # Status Metrics (Uptime, Stepper, RSSI)
        metrics_layout = QGridLayout()
        
        self.lbl_stepper_pos = QLabel("Stepper Pos: 0 steps")
        self.lbl_uptime = QLabel("Uptime: 0s")
        self.lbl_rssi = QLabel("RSSI: -- dBm")

        for lbl in (self.lbl_stepper_pos, self.lbl_uptime, self.lbl_rssi):
            lbl.setStyleSheet("background: #0f172a; border: 1px solid #334155; padding: 6px; border-radius: 6px; font-size: 12px;")

        metrics_layout.addWidget(self.lbl_stepper_pos, 0, 0)
        metrics_layout.addWidget(self.lbl_uptime, 0, 1)
        metrics_layout.addWidget(self.lbl_rssi, 1, 0, 1, 2)

        group_layout.addLayout(metrics_layout)
        group_layout.addSpacing(10)

        # Real-time Log Console
        group_layout.addWidget(QLabel("Rover Event Log Console:"))
        self.log_console = QTextEdit()
        self.log_console.setReadOnly(True)
        self.log_console.setFixedHeight(120)
        self.log_console.setStyleSheet("background-color: #050811; color: #00f2fe; font-family: monospace; border: 1px solid #1e293b;")
        group_layout.addWidget(self.log_console)

        group.setLayout(group_layout)
        main_layout.addWidget(group)
        self.setLayout(main_layout)

    @pyqtSlot(TelemetryData)
    def on_telemetry_received(self, data: TelemetryData):
        self.battery_widget.update_voltage(data.battery_voltage)
        self.motors_widget.update_motors(data.motor_speeds)

        status_txt = "MOVING" if data.stepper_moving else "IDLE"
        self.lbl_stepper_pos.setText(f"Stepper: {data.stepper_position} ({status_txt})")
        self.lbl_uptime.setText(f"Uptime: {data.uptime_seconds}s")
        self.lbl_rssi.setText(f"Wi-Fi RSSI: {data.rssi} dBm")

    def append_log(self, message: str):
        self.log_console.append(message)
