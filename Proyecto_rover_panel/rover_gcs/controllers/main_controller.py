from PyQt6.QtCore import QObject, pyqtSlot
from models.rover_state import RoverStateModel
from models.udp_client import UDPCommThread
from models.joystick_manager import JoystickThread
from views.main_window import MainWindow

class MainController(QObject):
    """
    MVC Controller orchestrating User Input (Gamepad / UI Sliders),
    Data Model updates, and UDP socket threads.
    """
    def __init__(self, model: RoverStateModel, view: MainWindow):
        super().__init__()
        self.model = model
        self.view = view

        # 1. Initialize Network & Joystick Threads
        self.udp_thread = UDPCommThread(self.model)
        self.joystick_thread = JoystickThread()

        # 2. Connect Model Signals to View
        self.model.telemetry_updated.connect(self.view.telemetry_widget.on_telemetry_received)
        self.model.connection_status_changed.connect(self.view.update_connection_badge)

        # 3. Connect View Control Signals to Model
        self.view.control_widget.drive_command_signal.connect(self._on_ui_drive_command)
        self.view.control_widget.servo_command_signal.connect(self._on_ui_servo_command)
        self.view.control_widget.stepper_command_signal.connect(self._on_ui_stepper_command)
        self.view.control_widget.emergency_stop_signal.connect(self._on_ui_estop)

        # 4. Connect Joystick Signals
        self.joystick_thread.drive_input_signal.connect(self._on_joystick_drive_input)
        self.joystick_thread.joystick_status_signal.connect(self._on_joystick_status)

        # Start Threads
        self.udp_thread.start()
        self.joystick_thread.start()

    @pyqtSlot(float, float)
    def _on_ui_drive_command(self, linear: float, angular: float):
        self.model.set_drive_command(linear, angular)

    @pyqtSlot(int, float)
    def _on_ui_servo_command(self, channel: int, angle: float):
        self.model.set_servo_angle(channel, angle)

    @pyqtSlot(int, int)
    def _on_ui_stepper_command(self, target: int, speed: int):
        self.model.set_stepper_command(target, speed)

    @pyqtSlot(bool)
    def _on_ui_estop(self, active: bool):
        self.model.set_emergency_stop(active)
        if active:
            self.view.telemetry_widget.append_log("[CRITICAL] EMERGENCY STOP ENGAGED!")
        else:
            self.view.telemetry_widget.append_log("[INFO] EMERGENCY STOP RESET. System Armed.")

    @pyqtSlot(float, float)
    def _on_joystick_drive_input(self, linear: float, angular: float):
        # Only override drive command if E-Stop is not active and non-zero joystick input
        if not self.model.command.emergency_stop:
            if abs(linear) > 1.0 or abs(angular) > 1.0:
                self.model.set_drive_command(linear, angular)

    @pyqtSlot(bool, str)
    def _on_joystick_status(self, connected: bool, text: str):
        self.view.control_widget.update_gamepad_label(connected, text)

    def shutdown(self):
        """Cleanup threads on app exit."""
        self.udp_thread.stop()
        self.joystick_thread.stop()
