'use strict';

/*
 * Iconos HUD del paquete de elementos visuales (Proyecto_rover_panel/elementos_visuales).
 * Van incrustados en vez de como <img> para que hereden el color del texto
 * (currentColor) y funcionen abriendo el panel desde disco (file://).
 * Uso en el HTML: <i class="icon" data-icon="radar"></i>
 */
const ICONS = {
    // nasa_saturn_resonance_rings
    logo: `<svg viewBox="0 0 240 160" fill="none" stroke="currentColor"><g stroke-linecap="round"><ellipse cx="120" cy="80" rx="104" ry="34" stroke-width="3"/><ellipse cx="120" cy="80" rx="88" ry="28" stroke-width="2" stroke-dasharray="4 3"/><ellipse cx="120" cy="80" rx="70" ry="21" stroke-width="2"/><ellipse cx="120" cy="80" rx="52" ry="14" stroke-width="1.5"/></g><circle cx="120" cy="80" r="34" fill="var(--bg)" stroke-width="4"/><path d="M 120 46 A 34 34 0 0 1 120 114 A 18 34 0 0 0 120 46 Z" fill="currentColor" fill-opacity="0.3" stroke="none"/><circle cx="114" cy="72" r="4" fill="currentColor" stroke="none"/><circle cx="128" cy="86" r="5" fill="currentColor" stroke="none"/><circle cx="24" cy="74" r="5" fill="currentColor" stroke="none"/></svg>`,

    // deep_space_radar_dish
    radar: `<svg viewBox="0 0 140 140" fill="currentColor"><path d="M 50 120 C 50 110 58 102 70 102 C 82 102 90 110 90 120 Z"/><rect x="42" y="120" width="56" height="8" rx="2"/><circle cx="70" cy="98" r="7"/><g transform="rotate(-38 70 70)"><path d="M 30 70 C 30 42 70 42 70 42 C 70 42 110 42 110 70 C 95 62 75 60 70 60 C 65 60 45 62 30 70 Z"/><line x1="70" y1="60" x2="70" y2="34" stroke="currentColor" stroke-width="3" stroke-linecap="round"/><circle cx="70" cy="32" r="5"/></g></svg>`,

    // deep_space_probe_satellite
    satellite: `<svg viewBox="0 0 180 140" fill="none" stroke="currentColor" stroke-width="3"><rect x="74" y="48" width="32" height="44" rx="4" fill="currentColor" fill-opacity="0.2"/><circle cx="90" cy="70" r="6" fill="currentColor" stroke="none"/><path d="M 78 40 C 78 30 102 30 102 40" stroke-width="4"/><line x1="90" y1="40" x2="90" y2="48" stroke-width="4"/><circle cx="90" cy="24" r="4" fill="currentColor" stroke="none"/><g transform="translate(10, 52)"><rect width="54" height="36" rx="2" stroke-width="3.5"/><line x1="18" y1="0" x2="18" y2="36"/><line x1="36" y1="0" x2="36" y2="36"/><line x1="0" y1="18" x2="64" y2="18"/></g><g transform="translate(116, 52)"><rect width="54" height="36" rx="2" stroke-width="3.5"/><line x1="18" y1="0" x2="18" y2="36"/><line x1="36" y1="0" x2="36" y2="36"/><line x1="-10" y1="18" x2="54" y2="18"/></g></svg>`,

    // rocket_launch_solid
    rocket: `<svg viewBox="0 0 120 160" fill="currentColor"><path d="M 60 12 C 64 24 74 48 74 88 L 46 88 C 46 48 56 24 60 12 Z"/><circle cx="60" cy="52" r="7" fill="var(--btn-fill, var(--bg))" stroke="currentColor" stroke-width="2.5"/><polygon points="46,68 24,106 36,112 46,92"/><polygon points="74,68 96,106 84,112 74,92"/><polygon points="52,94 60,138 68,94"/><polygon points="44,94 48,124 52,94" opacity="0.7"/><polygon points="68,94 72,124 76,94" opacity="0.7"/></svg>`,

    // hud-targeting-circle
    target: `<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5"><path d="M 32 20 A 36 36 0 0 1 68 20"/><path d="M 80 32 A 36 36 0 0 1 80 68"/><path d="M 68 80 A 36 36 0 0 1 32 80"/><path d="M 20 68 A 36 36 0 0 1 20 32"/><line x1="50" y1="4" x2="50" y2="16"/><line x1="50" y1="84" x2="50" y2="96"/><line x1="4" y1="50" x2="16" y2="50"/><line x1="84" y1="50" x2="96" y2="50"/><circle cx="50" cy="50" r="8" fill="currentColor" stroke="none"/></svg>`,

    // hud_alert_hazard_badge (solo el hexagono central)
    hazard: `<svg viewBox="0 0 100 70" fill="none" stroke="currentColor"><polygon points="22,6 78,6 94,35 78,64 22,64 6,35" stroke-width="6"/><line x1="50" y1="18" x2="50" y2="40" stroke-width="8" stroke-linecap="round"/><circle cx="50" cy="52" r="5" fill="currentColor" stroke="none"/></svg>`,

    // hud_crosshair_lock_reticle
    reticle: `<svg viewBox="0 0 160 160" fill="none" stroke="currentColor"><path d="M 32 46 L 32 32 L 46 32 M 128 46 L 128 32 L 114 32 M 32 114 L 32 128 L 46 128 M 128 114 L 128 128 L 114 128" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/><circle cx="80" cy="80" r="54" stroke-width="3" stroke-dasharray="8 6"/><circle cx="80" cy="80" r="30" stroke-width="5"/><line x1="80" y1="8" x2="80" y2="28" stroke-width="6"/><line x1="80" y1="132" x2="80" y2="152" stroke-width="6"/><line x1="8" y1="80" x2="28" y2="80" stroke-width="6"/><line x1="132" y1="80" x2="152" y2="80" stroke-width="6"/><circle cx="80" cy="80" r="7" fill="currentColor" stroke="none"/></svg>`,

    // hud-sensor-waveform
    wave: `<svg viewBox="0 0 120 80" fill="currentColor"><rect x="10" y="32" width="8" height="16" rx="4"/><rect x="22" y="22" width="8" height="36" rx="4"/><rect x="34" y="14" width="8" height="52" rx="4"/><rect x="46" y="8" width="8" height="64" rx="4"/><rect x="58" y="4" width="8" height="72" rx="4"/><rect x="70" y="8" width="8" height="64" rx="4"/><rect x="82" y="14" width="8" height="52" rx="4"/><rect x="94" y="22" width="8" height="36" rx="4"/><rect x="106" y="32" width="8" height="16" rx="4"/></svg>`,

    // astronaut_helmet_visor
    helmet: `<svg viewBox="0 0 160 160" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round"><path d="M 40 128 C 24 116 20 86 24 60 C 30 28 54 16 80 16 C 106 16 130 28 136 60 C 140 86 136 116 120 128" stroke-width="6"/><rect x="36" y="128" width="88" height="14" rx="4" fill="currentColor" fill-opacity="0.2" stroke-width="5"/><path d="M 42 66 C 42 46 58 40 80 40 C 102 40 118 46 118 66 C 118 94 104 108 80 108 C 56 108 42 94 42 66 Z" fill="currentColor" fill-opacity="0.25" stroke-width="6"/><path d="M 54 54 C 64 48 80 48 94 52 C 90 56 78 56 64 62 C 58 64 54 60 54 54 Z" fill="currentColor" stroke="none"/><line x1="138" y1="68" x2="148" y2="34" stroke-width="5"/><circle cx="148" cy="34" r="5" fill="currentColor" stroke="none"/></svg>`,

    // star-compass-8point-1e
    compass: `<svg viewBox="0 0 100 100" fill="currentColor"><path d="M 50 4 L 55 42 L 50 46 L 45 42 Z"/><path d="M 50 96 L 55 58 L 50 54 L 45 58 Z"/><path d="M 4 50 L 42 45 L 46 50 L 42 55 Z"/><path d="M 96 50 L 58 45 L 54 50 L 58 55 Z"/><path d="M 22 22 L 44 39 L 39 44 Z"/><path d="M 78 22 L 56 39 L 61 44 Z"/><path d="M 22 78 L 44 61 L 39 56 Z"/><path d="M 78 78 L 56 61 L 61 56 Z"/></svg>`,

    // glyph-airlock-ring
    gear: `<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="5"><circle cx="50" cy="50" r="38"/><circle cx="50" cy="50" r="18"/><line x1="68" y1="50" x2="92" y2="50"/><line x1="62.7" y1="62.7" x2="79.7" y2="79.7"/><line x1="50" y1="68" x2="50" y2="92"/><line x1="37.3" y1="62.7" x2="20.3" y2="79.7"/><line x1="32" y1="50" x2="8" y2="50"/><line x1="37.3" y1="37.3" x2="20.3" y2="20.3"/><line x1="50" y1="32" x2="50" y2="8"/><line x1="62.7" y1="37.3" x2="79.7" y2="20.3"/></svg>`,

    // hud-chevrons-triple
    chevrons: `<svg viewBox="0 0 100 80" fill="currentColor"><polygon points="12,10 32,40 12,70 24,70 44,40 24,10"/><polygon points="36,10 56,40 36,70 48,70 68,40 48,10"/><polygon points="60,10 80,40 60,70 72,70 92,40 72,10"/></svg>`,

    // wireframe-globe-isometric
    globe: `<svg viewBox="0 0 120 120" fill="none" stroke="currentColor" stroke-width="4"><circle cx="60" cy="60" r="50" stroke-width="5"/><ellipse cx="60" cy="60" rx="30" ry="50"/><line x1="60" y1="10" x2="60" y2="110"/><ellipse cx="60" cy="60" rx="50" ry="16"/></svg>`,

    // slashed-planet-axis (desconectar)
    wifi: `<svg viewBox="0 0 120 100" fill="none" stroke="currentColor" stroke-width="9" stroke-linecap="round"><path d="M 10 38 C 38 10 82 10 110 38"/><path d="M 28 56 C 48 36 72 36 92 56"/><path d="M 45 73 C 54 64 66 64 75 73"/><circle cx="60" cy="88" r="7" fill="currentColor" stroke="none"/></svg>`,

    eject: `<svg viewBox="0 0 100 100" fill="none" stroke="currentColor" stroke-width="8" stroke-linecap="round"><circle cx="50" cy="50" r="28"/><line x1="18" y1="18" x2="82" y2="82"/></svg>`,
};

// sun_radiant_corona: el sol de la marca USACH Space Program. Los 24 rayos se
// generan para no repetir 24 lineas a mano (largos alternos, como el original).
ICONS.sun = (() => {
    let rays = '';
    for (let i = 0; i < 24; i++) {
        const a = (i * 15) * Math.PI / 180;
        const r1 = 36, r2 = i % 2 ? 44 : 52;
        const p = (r) => `${(70 + r * Math.cos(a)).toFixed(1)}`;
        const q = (r) => `${(70 + r * Math.sin(a)).toFixed(1)}`;
        rays += `<line x1="${p(r1)}" y1="${q(r1)}" x2="${p(r2)}" y2="${q(r2)}"/>`;
    }
    return `<svg viewBox="0 0 140 140" fill="none" stroke="currentColor" stroke-linecap="round">` +
           `<circle cx="70" cy="70" r="30" stroke-width="7"/><circle cx="70" cy="70" r="18" fill="currentColor" stroke="none"/>` +
           `<g stroke-width="5">${rays}</g></svg>`;
})();

const Icons = {
    mount(root = document) {
        root.querySelectorAll('[data-icon]').forEach((el) => {
            const svg = ICONS[el.dataset.icon];
            if (svg) {
                el.innerHTML = svg;
                el.setAttribute('aria-hidden', 'true');
            }
        });
    },
};
