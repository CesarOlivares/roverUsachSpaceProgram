from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox, QPushButton, QLabel, QSlider, QSpinBox, QGridLayout, QTabWidget, QCheckBox
)
from PyQt6.QtCore import Qt, pyqtSignal

class ControlDashboardWidget(QWidget):
    """
    Main Control Panel widget for manual drive commands, E-Stop, Stepper driver controls, and Servo channels.
    """
    drive_command_signal = pyqtSignal(float, float)        # linear, angular
    servo_command_signal = pyqtSignal(int, float)          # channel, angle
    stepper_command_signal = pyqtSignal(int, int)          # target_step, speed_hz
    emergency_stop_signal = pyqtSignal(bool)              # active state

    def __init__(self):
        super().__init__()
        main_layout = QVBoxLayout()

        # 1. EMERGENCY STOP BAR
        estop_layout = QHBoxLayout()
        self.btn_estop = QPushButton("EMERGENCY STOP (E-STOP)")
        self.btn_estop.setObjectName("EStopButton")
        self.btn_estop.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_estop.clicked.connect(self._on_estop_clicked)
        estop_layout.addWidget(self.btn_estop)
        main_layout.addLayout(estop_layout)

        main_layout.addSpacing(10)

        # 2. TABBED CONTROLS
        self.tabs = QTabWidget()

        # Tab A: Drive & Steering
        self.tab_drive = QWidget()
        self._init_drive_tab()
        self.tabs.addTab(self.tab_drive, "Rover Traction Drive")

        # Tab B: Stepper Mechanism (DRV8825)
        self.tab_stepper = QWidget()
        self._init_stepper_tab()
        self.tabs.addTab(self.tab_stepper, "DRV8825 Stepper Motor")

        # Tab C: PCA9685 Servo Expander (16 Channels)
        self.tab_servos = QWidget()
        self._init_servos_tab()
        self.tabs.addTab(self.tab_servos, "PCA9685 Servos (16-Ch)")

        main_layout.addWidget(self.tabs)
        self.setLayout(main_layout)

        self.estop_active = False

    def _init_drive_tab(self):
        layout = QVBoxLayout()
        group = QGroupBox("MANUAL TRACTION SLIDERS (DIFFERENTIAL DRIVE)")
        g_layout = QGridLayout()

        # Linear speed slider
        g_layout.addWidget(QLabel("Linear Throttle (-100% to +100%):"), 0, 0)
        self.slider_linear = QSlider(Qt.Orientation.Horizontal)
        self.slider_linear.setRange(-100, 100)
        self.slider_linear.setValue(0)
        self.lbl_linear_val = QLabel("0%")
        self.slider_linear.valueChanged.connect(self._on_drive_sliders_changed)
        g_layout.addWidget(self.slider_linear, 0, 1)
        g_layout.addWidget(self.lbl_linear_val, 0, 2)

        # Angular steering slider
        g_layout.addWidget(QLabel("Steering Yaw (-100% to +100%):"), 1, 0)
        self.slider_angular = QSlider(Qt.Orientation.Horizontal)
        self.slider_angular.setRange(-100, 100)
        self.slider_angular.setValue(0)
        self.lbl_angular_val = QLabel("0%")
        self.slider_angular.valueChanged.connect(self._on_drive_sliders_changed)
        g_layout.addWidget(self.slider_angular, 1, 1)
        g_layout.addWidget(self.lbl_angular_val, 1, 2)

        # Stop Traction button
        btn_stop = QPushButton("Stop Traction")
        btn_stop.clicked.connect(self._stop_drive_sliders)
        g_layout.addWidget(btn_stop, 2, 0, 1, 3)

        group.setLayout(g_layout)
        layout.addWidget(group)

        # Mode Info
        self.lbl_gamepad_status = QLabel("Gamepad Status: Standby / Polling...")
        self.lbl_gamepad_status.setStyleSheet("background: #0f172a; padding: 10px; border-radius: 8px; color: #38bdf8;")
        layout.addWidget(self.lbl_gamepad_status)
        layout.addStretch()

        self.tab_drive.setLayout(layout)

    def _init_stepper_tab(self):
        layout = QVBoxLayout()
        group = QGroupBox("DRV8825 STEPPER MOTOR CONTROLLER")
        g_layout = QGridLayout()

        g_layout.addWidget(QLabel("Target Step Position:"), 0, 0)
        self.spin_stepper_target = QSpinBox()
        self.spin_stepper_target.setRange(-100000, 100000)
        self.spin_stepper_target.setValue(0)
        g_layout.addWidget(self.spin_stepper_target, 0, 1)

        g_layout.addWidget(QLabel("Speed (Steps/sec - Hz):"), 1, 0)
        self.spin_stepper_speed = QSpinBox()
        self.spin_stepper_speed.setRange(100, 5000)
        self.spin_stepper_speed.setValue(1000)
        g_layout.addWidget(self.spin_stepper_speed, 1, 1)

        btn_move_to = QPushButton("Move to Absolute Step Target")
        btn_move_to.clicked.connect(self._on_stepper_move)
        g_layout.addWidget(btn_move_to, 2, 0, 1, 2)

        # Quick relative step buttons
        rel_layout = QHBoxLayout()
        btn_rev_100 = QPushButton("<< -500 Steps")
        btn_fwd_100 = QPushButton("+500 Steps >>")
        btn_rev_100.clicked.connect(lambda: self._on_stepper_relative(-500))
        btn_fwd_100.clicked.connect(lambda: self._on_stepper_relative(500))
        rel_layout.addWidget(btn_rev_100)
        rel_layout.addWidget(btn_fwd_100)

        g_layout.addLayout(rel_layout, 3, 0, 1, 2)

        group.setLayout(g_layout)
        layout.addWidget(group)
        layout.addStretch()
        self.tab_stepper.setLayout(layout)

    def _init_servos_tab(self):
        layout = QVBoxLayout()
        group = QGroupBox("PCA9685 16-CHANNEL SERVO EXPANDER")
        grid = QGridLayout()

        self.servo_sliders = []
        self.servo_val_labels = []

        for ch in range(16):
            row = ch // 2
            col_offset = (ch % 2) * 3

            lbl_ch = QLabel(f"Ch {ch:02d}:")
            slider = QSlider(Qt.Orientation.Horizontal)
            slider.setRange(0, 180)
            slider.setValue(90)
            val_lbl = QLabel("90°")
            val_lbl.setFixedWidth(35)

            slider.valueChanged.connect(lambda val, channel=ch, label=val_lbl: self._on_servo_changed(channel, val, label))

            grid.addWidget(lbl_ch, row, col_offset)
            grid.addWidget(slider, row, col_offset + 1)
            grid.addWidget(val_lbl, row, col_offset + 2)

            self.servo_sliders.append(slider)
            self.servo_val_labels.append(val_lbl)

        group.setLayout(grid)
        layout.addWidget(group)
        self.tab_servos.setLayout(layout)

    def _on_drive_sliders_changed(self):
        lin = float(self.slider_linear.value())
        ang = float(self.slider_angular.value())
        self.lbl_linear_val.setText(f"{int(lin)}%")
        self.lbl_angular_val.setText(f"{int(ang)}%")
        self.drive_command_signal.emit(lin, ang)

    def _stop_drive_sliders(self):
        self.slider_linear.setValue(0)
        self.slider_angular.setValue(0)
        self.drive_command_signal.emit(0.0, 0.0)

    def _on_stepper_move(self):
        target = self.spin_stepper_target.value()
        speed = self.spin_stepper_speed.value()
        self.stepper_command_signal.emit(target, speed)

    def _on_stepper_relative(self, delta: int):
        curr = self.spin_stepper_target.value()
        new_target = curr + delta
        self.spin_stepper_target.setValue(new_target)
        self._on_stepper_move()

    def _on_servo_changed(self, channel: int, val: int, label: QLabel):
        label.setText(f"{val}°")
        self.servo_command_signal.emit(channel, float(val))

    def _on_estop_clicked(self):
        self.estop_active = not self.estop_active
        if self.estop_active:
            self.btn_estop.setText("RESET E-STOP (ARM ROVER)")
            self.btn_estop.setStyleSheet("background: #15803d; border-color: #22c55e;")
            self._stop_drive_sliders()
        else:
            self.btn_estop.setText("EMERGENCY STOP (E-STOP)")
            self.btn_estop.setStyleSheet("")

        self.emergency_stop_signal.emit(self.estop_active)

    def update_gamepad_label(self, connected: bool, text: str):
        color = "#10b981" if connected else "#f59e0b"
        self.lbl_gamepad_status.setText(f"Gamepad: {text}")
        self.lbl_gamepad_status.setStyleSheet(f"background: #0f172a; padding: 10px; border-radius: 8px; color: {color};")
