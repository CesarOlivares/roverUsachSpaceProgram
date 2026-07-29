"""
Modern Rich Dark Mode QSS Stylesheet for 6x6 Rover Ground Control Station.
Features glassmorphic panels, vibrant accent colors, clear hierarchy, and responsive UI feedback.
"""

DARK_THEME_QSS = """
/* Main Window & Core Colors */
QMainWindow {
    background-color: #0b0f19;
    color: #e2e8f0;
}

QWidget {
    font-family: 'Segoe UI', 'Outfit', 'Inter', sans-serif;
    font-size: 13px;
    color: #cbd5e1;
}

/* Glassmorphic Cards & Group Boxes */
QGroupBox {
    background: rgba(15, 23, 42, 0.75);
    border: 1px solid rgba(255, 255, 255, 0.1);
    border-radius: 12px;
    margin-top: 1.2em;
    padding-top: 15px;
    font-weight: bold;
    color: #00f2fe;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 15px;
    padding: 0 5px;
    background-color: #0b0f19;
    color: #38bdf8;
    font-size: 14px;
}

/* Buttons */
QPushButton {
    background: linear-gradient(135deg, #1e293b 0%, #0f172a 100%);
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 8px 16px;
    color: #f8fafc;
    font-weight: 600;
}

QPushButton:hover {
    background: #334155;
    border-color: #38bdf8;
    color: #ffffff;
}

QPushButton:pressed {
    background: #0284c7;
    border-color: #0284c7;
}

/* Emergency Stop Button */
QPushButton#EStopButton {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #dc2626, stop:1 #b91c1c);
    border: 2px solid #ef4444;
    border-radius: 10px;
    color: #ffffff;
    font-size: 16px;
    font-weight: 800;
    padding: 12px 24px;
}

QPushButton#EStopButton:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #ef4444, stop:1 #dc2626);
    border-color: #f87171;
}

/* Sliders */
QSlider::groove:horizontal {
    border: 1px solid #334155;
    height: 8px;
    background: #1e293b;
    border-radius: 4px;
}

QSlider::sub-page:horizontal {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00f2fe, stop:1 #4facfe);
    border-radius: 4px;
}

QSlider::handle:horizontal {
    background: #ffffff;
    border: 2px solid #38bdf8;
    width: 18px;
    height: 18px;
    margin: -6px 0;
    border-radius: 9px;
}

QSlider::handle:horizontal:hover {
    background: #38bdf8;
}

/* Progress Bars (Battery & Signal) */
QProgressBar {
    border: 1px solid #334155;
    border-radius: 6px;
    text-align: center;
    background-color: #0f172a;
    color: #ffffff;
    font-weight: bold;
}

QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #10b981, stop:1 #34d399);
    border-radius: 5px;
}

/* Status Indicators */
QLabel#ConnectionStatus {
    font-weight: bold;
    font-size: 14px;
    padding: 6px 12px;
    border-radius: 6px;
}

/* Text Inputs & Spinboxes */
QSpinBox, QDoubleSpinBox, QLineEdit {
    background-color: #0f172a;
    border: 1px solid #334155;
    border-radius: 6px;
    padding: 4px 8px;
    color: #f8fafc;
}

QSpinBox:focus, QDoubleSpinBox:focus, QLineEdit:focus {
    border-color: #38bdf8;
}
"""
