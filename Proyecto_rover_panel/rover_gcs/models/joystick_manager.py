import os
import time
from PyQt6.QtCore import QThread, pyqtSignal

# Suppress Pygame welcome banner
os.environ['PYGAME_HIDE_SUPPORT_PROMPT'] = "1"
import pygame

class JoystickThread(QThread):
    """
    Background QThread polling Pygame Hardware Gamepad / Controller inputs at 50Hz.
    Emits joystick drive axis updates and button trigger events.
    """
    drive_input_signal = pyqtSignal(float, float) # linear (-100..100), angular (-100..100)
    button_pressed_signal = pyqtSignal(int)       # button index
    joystick_status_signal = pyqtSignal(bool, str)# connected, name

    def __init__(self, deadzone: float = 0.08):
        super().__init__()
        self.deadzone = deadzone
        self.running = True
        self.joystick = None

    def run(self):
        pygame.init()
        pygame.joystick.init()

        last_check = 0

        while self.running:
            now = time.time()
            pygame.event.pump()

            # Periodically scan for connected controllers
            if self.joystick is None or not self.joystick.get_init():
                if now - last_check > 1.0:
                    last_check = now
                    if pygame.joystick.get_count() > 0:
                        try:
                            self.joystick = pygame.joystick.Joystick(0)
                            self.joystick.init()
                            name = self.joystick.get_name()
                            self.joystick_status_signal.emit(True, f"Gamepad Connected: {name}")
                        except Exception as e:
                            self.joystick = None
                            self.joystick_status_signal.emit(False, "No Gamepad Detected")
                    else:
                        self.joystick_status_signal.emit(False, "No Gamepad Detected")

            if self.joystick and self.joystick.get_init():
                try:
                    # Standard mapping:
                    # Axis 1: Left Y (Linear speed)
                    # Axis 0: Left X or Right X (Angular steering speed)
                    raw_y = -self.joystick.get_axis(1) # Invert Y so up = forward
                    raw_x = self.joystick.get_axis(0 if self.joystick.get_numaxes() < 4 else 2)

                    linear = self._apply_deadzone(raw_y) * 100.0
                    angular = self._apply_deadzone(raw_x) * 100.0

                    self.drive_input_signal.emit(linear, angular)

                    # Poll buttons for E-Stop or special actions
                    for b in range(self.joystick.get_numbuttons()):
                        if self.joystick.get_button(b):
                            self.button_pressed_signal.emit(b)

                except Exception as e:
                    self.joystick = None
                    self.joystick_status_signal.emit(False, f"Gamepad Error: {e}")

            time.sleep(0.02) # 50 Hz poll rate

        pygame.joystick.quit()
        pygame.quit()

    def _apply_deadzone(self, value: float) -> float:
        if abs(value) < self.deadzone:
            return 0.0
        # Re-scale linearly outside deadzone
        sign = 1.0 if value > 0 else -1.0
        return sign * ((abs(value) - self.deadzone) / (1.0 - self.deadzone))

    def stop(self):
        self.running = False
        self.wait()
