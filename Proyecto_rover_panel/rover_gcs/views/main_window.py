from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout, QVBoxLayout, QLabel, QFrame, QSplitter
)
from PyQt6.QtCore import Qt, pyqtSlot

from views.styles import DARK_THEME_QSS
from views.widgets_telemetry import TelemetryDashboardWidget
from views.widgets_control import ControlDashboardWidget

class MainWindow(QMainWindow):
    """
    Primary Ground Control Station Dashboard Window for 6x6 Rover.
    Combines live telemetry HUD and interactive control panels.
    """
    def __init__(self):
        super().__init__()
        self.setWindowTitle("ROVER 6x6 GROUND CONTROL STATION (ESP32-S3 + PYQT6)")
        self.resize(1280, 800)
        self.setStyleSheet(DARK_THEME_QSS)

        # Central Widget & Main Layout
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(15, 15, 15, 15)

        # 1. HEADER BAR
        header = QFrame()
        header.setStyleSheet("background: rgba(15, 23, 42, 0.9); border: 1px solid rgba(255, 255, 255, 0.1); border-radius: 10px;")
        header_layout = QHBoxLayout(header)

        title_lbl = QLabel("ROVER 6x6 MECHATRONIC CONTROL SYSTEM")
        title_lbl.setStyleSheet("font-size: 18px; font-weight: bold; color: #00f2fe; letter-spacing: 1px;")

        self.status_badge = QLabel("DISCONNECTED")
        self.status_badge.setObjectName("ConnectionStatus")
        self.status_badge.setStyleSheet("background: #ef4444; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 4px 12px;")

        header_layout.addWidget(title_lbl)
        header_layout.addStretch()
        header_layout.addWidget(self.status_badge)
        main_layout.addWidget(header)

        main_layout.addSpacing(10)

        # 2. SPLITTER MAIN VIEW (Control Left, Telemetry Right)
        splitter = QSplitter(Qt.Orientation.Horizontal)

        self.control_widget = ControlDashboardWidget()
        self.telemetry_widget = TelemetryDashboardWidget()

        splitter.addWidget(self.control_widget)
        splitter.addWidget(self.telemetry_widget)
        splitter.setSizes([640, 640])

        main_layout.addWidget(splitter)

    @pyqtSlot(bool, str)
    def update_connection_badge(self, connected: bool, message: str):
        if connected:
            self.status_badge.setText(f"ONLINE | {message}")
            self.status_badge.setStyleSheet("background: #10b981; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 4px 12px;")
        else:
            self.status_badge.setText("DISCONNECTED")
            self.status_badge.setStyleSheet("background: #ef4444; color: #ffffff; font-weight: bold; border-radius: 6px; padding: 4px 12px;")

        self.telemetry_widget.append_log(f"[SYS] {message}")
