'use strict';

/* Cableado: crea el modelo, las vistas y los controladores y une intenciones con acciones. */
document.addEventListener('DOMContentLoaded', () => {
    const model = new RoverModel();
    const link = new Link();

    // Vistas
    const login = new LoginView();
    const header = new HeaderView(model);
    const operation = new OperationView(model);
    const schematic = new SchematicView(model);
    const diagView = new DiagnosticsView(model);
    const preflightView = new PreflightView();
    new PinTableView(model);
    new LinkMonitorView(model);

    // Controladores
    const drive = new DriveController(model, link);
    const safety = new SafetyController(model, link);
    const diagnostics = new DiagnosticsController(model, link, drive);
    const preflight = new PreflightController(model, link, preflightView, drive, diagnostics, safety);
    const connection = new ConnectionController(model, link, login, { drive, preflight, safety });

    drive.bindInputs();
    safety.bindInputs();

    // Intenciones -> controladores
    login.on('serial', () => connection.connectSerial());
    login.on('wifi', ({ ip, port }) => connection.connectWifi(ip, port));
    login.on('demo', () => connection.connectDemo());

    header.on('disconnect', () => connection.disconnect());

    operation.on('toggle-arm', () => drive.toggleArm());
    operation.on('preflight', () => preflight.start());
    operation.on('estop', () => safety.emergencyStop());

    schematic.on('hold', ({ motor, sign }) => diagnostics.beginManualTest(motor, sign * CONFIG.TEST_SPEED));
    schematic.on('release', () => diagnostics.releaseManualTest());

    diagView.on('motor', ({ motor, speed }) => diagnostics.setMotor(motor, speed));
    diagView.on('stop-all', () => {
        diagView.resetMotorSliders();
        diagnostics.stopAllMotors();
    });
    diagView.on('servo', ({ channel, angle }) => diagnostics.setServo(channel, angle));
    diagView.on('jog', ({ steps }) => diagnostics.jogStepper(steps));

    connection.autoConnect();
});
