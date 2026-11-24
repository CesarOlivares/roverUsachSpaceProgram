'use strict';

/* Cableado: crea el modelo, las vistas y los controladores y une intenciones con acciones. */
document.addEventListener('DOMContentLoaded', () => {
    Icons.mount();

    const model = new RoverModel();
    const stats = new LinkStats(model);
    const link = new Link();

    // Vistas
    const login = new LoginView();
    const header = new HeaderView(model);
    const operation = new OperationView(model);
    const schematic = new SchematicView(model);
    const diagView = new DiagnosticsView(model);
    const preflightView = new PreflightView();
    const mastView = new MastView(model);
    const signalView = new SignalView(model, stats);
    const wifiView = new WifiView(model);
    new PinTableView(model);
    new LinkMonitorView(model);

    // Controladores
    const drive = new DriveController(model, link);
    const safety = new SafetyController(model, link);
    const diagnostics = new DiagnosticsController(model, link, drive);
    const mast = new MastController(model, diagnostics);
    const preflight = new PreflightController(model, link, preflightView, drive, diagnostics, safety);
    const connection = new ConnectionController(model, link, login, { drive, preflight, safety });
    const linkWatch = new LinkWatchController(model, link, connection);
    const wifi = new WifiController(model, link);

    drive.bindInputs();
    safety.bindInputs();
    linkWatch.start();

    // Intenciones -> controladores
    login.on('serial', () => connection.connectSerial());
    login.on('wifi', (host) => connection.connectWifi(host));
    login.on('demo', () => connection.connectDemo());

    wifiView.on('join', (creds) => wifi.join(creds));
    wifiView.on('scan', () => wifi.scan());
    wifiView.on('forget', () => wifi.forget());

    header.on('disconnect', () => connection.disconnect('Panel desconectado.'));

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

    mastView.on('nod', () => mast.nod());

    // Señal: exportar el registro y, en simulacion, provocar cortes de prueba.
    signalView.on('export', () => {
        const blob = new Blob([stats.toCsv()], { type: 'text/csv;charset=utf-8' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = `enlace_rover_${new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-')}.csv`;
        a.click();
        URL.revokeObjectURL(a.href);
    });
    signalView.on('simulate', ({ ms }) => {
        if (link.transport instanceof DemoTransport) link.transport.simulateDrop(ms);
    });
});
