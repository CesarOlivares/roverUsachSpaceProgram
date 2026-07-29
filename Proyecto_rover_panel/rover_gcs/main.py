import sys
from PyQt6.QtWidgets import QApplication
from models.rover_state import RoverStateModel
from views.main_window import MainWindow
from controllers.main_controller import MainController

def main():
    """
    Ground Control Station Entry Point.
    """
    app = QApplication(sys.argv)
    app.setApplicationName("Rover 6x6 GCS")

    # Instantiate MVC architecture components
    model = RoverStateModel()
    view = MainWindow()
    controller = MainController(model, view)

    # Show dashboard UI
    view.show()

    # Clean shutdown handling
    app.aboutToQuit.connect(controller.shutdown)
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
