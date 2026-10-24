# Flydigi BS2 Pro Control for Linux

![GitHub license](https://img.shields.io/github/license/ddoblejotadev/flydigi-bs2pro-linux.svg)
![GitHub repo size](https://img.shields.io/github/repo-size/ddoblejotadev/flydigi-bs2pro-linux.svg)
![GitHub stars](https://img.shields.io/github/stars/ddoblejotadev/flydigi-bs2pro-linux.svg?style=social)
![GitHub last commit](https://img.shields.io/github/last-commit/ddoblejotadev/flydigi-bs2pro-linux.svg)

A native Qt 6 / QML control panel for Flydigi BS-series laptop coolers, built for
Linux desktops. It provides the control surface of the Windows-only Flydigi BS2 Pro app,
with **no Wine and no Electron in the loop**.

The application connects to the `flydigictld` daemon over a newline-delimited JSON
unix socket and exposes every control the official Windows software offers, plus
 several Linux-specific improvements.

![Dashboard Screenshot](docs/screenshot.png)

---

## 📋 Table of Contents

- [About](#about)
- [REA Methodology](#rea-methodology)
- [Architecture](#architecture)
- [Requirements](#requirements)
- [Build & Install](#build--install)
- [Usage](#usage)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Lighting Payload](#lighting-payload)
- [Gear Presets](#gear-presets)
- [Resident Behaviour](#resident-behaviour)
- [Keyboard](#keyboard)
- [Credits](#credits)
- [License](#license)

---

## 🛠 About

**flydigi-bs2pro-linux** es un panel de control nativo Qt 6 / QML para ventiladores
laptop de la serie Flydigi BS. A diferencia de la app oficial de Windows (ejecutable
PE32 que requiere Wine), este proyecto:

- Se comunica con el daemon `flydigictld` por socket JSON
- Ofrece la misma superficie de controles que la app original
- Agrega comportamientos específicos de Linux (bandeja del sistema, bajo consumo)
- No requiere Wine, Electron ni emulación

### Logros clave (ODD):

| Especification | Estado |
|----------------|--------|
| S1: Conexión daemon socket | ✓ Completado |
| S2: Estado live + temperaturas | ✓ Completado |
| S3: Rows de sensores | ✓ Completado |
| S4: Control manual slider | ✓ Completado |
| S5: Gear presets (4×3 RPM) | ✓ Completado |
| S6: Standby (off/instant/delayed) | ✓ Completado |
| S7: Lighting editor (OFF/STATIC/EFFECT) | ✓ Completado con protocoleo REA |
| S8: Integración desktop (.desktop/.svg) | ✓ Completado |
| S9: Referencia vendor (Wine+asar) | ✓ Documentado |

---

## 🔍 REA Methodology

Este proyecto utiliza **REA (Reverse Engineering Assistant)** para el análisis
inverso del protocolo de comunicación del firmware de Flydigi. La metodología
aplicada incluyó:

### Investigación:

1. **Análisis del instalador Windows** - Usando Wine + `innoextract` para extraer
   el payload de `resources/app.asar` (app Electron + Vite + React)

2. **Inspección de locales** - Archivo `locales/en/translation.json` provee el
   vocabulario del vendor: `cooler_led_static/breath/gradient/rainbow`,
   `light_mode`, `brightness`, `gear_light_display`

3. **Análisis de packets HID** - `flydigictl list` → `/dev/hidraw2 BS2 Pro`;
   `status` → 1700 rpm, gear mode, supply low (2700 rpm cap)

4. **Documentación de firmware** - `docs/FIRMWARE.md` ya documenta la superficie
   completa de comandos leída del firmware

### Hallazgos críticos (L9-L10 REA):

| Hallazgo | Evidencia |
|----------|-----------|
| `LightMode` es enum interno etiquetado | L9: app enviaba `"mode":"effect"`, daemon lo rechazaba |
| Estructura wire format anidada | L10: `"mode":{"mode":"effect","effect":N}` es el formato correcto |
| Every lighting request rejected | L10: click mostraba cambio pero strip nunca respondía |

### Corrección implementada:

```json
// ❌ Rechazado por daemon (formato plano)
{"mode": "effect"}

// ✓ Aceptado por daemon (formato anidado)
{"mode": {"mode": "effect", "effect": 5}}
```

La corrección está en `src/backend.cpp:sendLighting()` y `applyStatus()`.

---

## 🏗 Architecture

```
flydigi-bs2pro-linux/
├── src/                    # Backend C++ (cliente daemon)
│   ├── backend.cpp         # Socket JSON, lighting protocol, status parsing
│   ├── backend.h         # Q_PROPERTY declarations, public API
│   └── main.cpp          # Entry point, Qt Application, system tray
├── qml/                    # Interfaz de usuario QML
│   ├── Controls.qml      # Gear presets, curve editor, lighting controls
│   ├── Main.qml          # Ventana principal, layout
│   ├── Gauge.qml         # Speed gauge widget
│   ├── TempList.qml      # Temperatura rows
│   ├── Theme.qml         # Theme colors (surface, line, accent, text, muted)
│   ├── Pill.qml          # Pill-shaped buttons selector
│   └── gauge.qss         # Estilo QSS para gauge
├── packaging/              # Archivos de distribución
│   ├── flydigi-bs2pro-linux.desktop
│   └── flydigi-bs2pro-linux.svg
├── odd/                    # Organic Driven Development tracking
│   ├── tasks/space-station-gui.md   # 7 tareas T1-T7
│   ├── tasks/official-cooling-modes.md  # 5 tareas S1-S5
│   └── tasks/official-ui-parity.md     # 5 tareas S1-S3
└── CMakeLists.txt          # Build system Qt 6
```

### Componentes principales:

| Componente | Responsabilidad |
|------------|-----------------|
| **Backend** | Cliente Unix domain socket, parsing de respuestas, envío de comandos |
| **QML** | UI rendering, user interaction, visual feedback |
| **Daemon** `flydigictld` | Propiedad del cooler, control real del hardware |
| **CMake** | Build system, instalación desktop entry + QML module |

### Decisiones de diseño:

- **Comunicación daemon vs app directa**: El daemon posee el cooler; la GUI solo
  consulta. Nunca se abre `/dev/hidraw2` directamente desde la aplicación.
- **Pacing de lighting 300ms**: Matching vendor client timing para que el daemon
  procese los reports sin perderlos.
- **Estructura JSON anidada**: `LightMode` es enum interno al `Lighting` struct,
  por lo que el wire format debe ser `{"mode":{"mode":"effect","effect":N}}`.
- **Consumo bajo**: Qt6 + QLocalSocket es eficiente; ~0.2% CPU quieto en bandeja.

---

## 📦 Requirements

| Dependencia | Versión mínima |
|-------------|----------------|
| Qt 6 | 6.5 |
| CMake | 3.21 |
| GCC / Clang | C++20 compatible |
| `flydigictld` daemon | Cualquier versión que provea el socket JSON |

### Sistemas operativos probados:

- **Linux** con Qt 6.5+ y daemon `flydigictl` corriendo
- **KDE Plasma**, **GNOME**, **XFCE** - bandeja del sistema compatible
- **Wayland** y **X11** - ambos soportados (algunos items de UI se comportan levemente diferente)

---

## 🛠 Build & Install

```sh
# 1. Clonar repo
git clone https://github.com/ddoblejotadev/flydigi-bs2pro-linux.git
cd flydigi-bs2pro-linux

# 2. Configurar y compilar
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 3. Instalar (prefix por defecto es $HOME/.local)
cmake --install build --prefix "$HOME/.local"

# 4. Verificar instalación
flydigi-bs2pro-linux --help
# Debe mostrar: Flydigi BS series cooler dashboard
```

### Instalación manual alternativa:

```sh
# Copiar binary a tu PATH
cp build/flydigi-bs2pro-linux ~/.local/bin/

# QML module directory
mkdir -p ~/.local/share/flydigi-bs2pro-linux/qml
cp -r qml/FlydigiSpace ~/.local/share/flydigi-bs2pro-linux/qml/

# Desktop entry + icon
cp packaging/flydigi-bs2pro-linux.desktop ~/.local/share/applications/
cp packaging/flydigi-bs2pro-linux.svg ~/.local/share/icons/hicolor/scalable/apps/
```

### Verificar que el daemon corre:

```sh
# Método automático (busca socket)
flydigi-bs2pro-linux

# Método manual (especificar socket)
flydigi-bs2pro-linux --socket /run/user/1000/flydigictl.sock
```

### Desinstalar:

```sh
cmake --uninstall build --prefix "$HOME/.local"
# O eliminación manual:
rm -f ~/.local/bin/flydigi-bs2pro-linux
rm -rf ~/.local/share/flydigi-bs2pro-linux/
rm -f ~/.local/share/applications/flydigi-bs2pro-linux.desktop
rm -f ~/.local/share/icons/hicolor/scalable/apps/flydigi-bs2pro-linux.svg
```

---

## 🚀 Usage

### Al iniciar:

1. Asegurarse de que `flydigictl` daemon esté corriendo
2. Ejecutar `flydigi-bs2pro-linux`
3. La ventana principal se abrirá y se minimizará a la bandeja del sistema

### Modos de operación:

| Modo | Cómo activar | Descripción |
|------|--------------|-------------|
| **AUTO** | Click en botón "AUTO" o tecla `M` | Curva inteligente del daemon |
| **MANUAL** | Slider RPM + tecla `C` | Fijar RPM manual (500-4000) |
| **GEAR** | Click en tarjetas de gear | 4 gears × 3 RPM oficiales |
| **STANDBY** | Seleccionar off/instant/delayed | Modo de reposo |

### Lighting editor:

- **OFF**: Strip apagado
- **COLOR**: Color sólido (selector de 8 colores presets)
- **EFFECT**: Uno de 5 efectos firmware (breath green/amber/red, gradient, rainbow)
- **Brightness**: Slider 0-100%
- **Gear indicators**: Checkbox para LEDs detrás de los gears

### Panel de curva personalizada:

Canvas-based con puntos arrastrables. Arrastra los puntos para cambiar la curva
de temperatura→RPM. Los puntos tienen límites de interpolación (temp_c siempre
creciente).

---

## ⌨ Keyboard Shortcuts

| Tecla | Acción |
|-------|--------|
| `M` | Hold current target speed (manual mode) |
| `C` | Give cooler back to its fan curves (auto mode) |
| `1`–`5` | Select strip animation (1=OFF, 2=breath green, 3=breath amber, 4=breath red, 5=gradient/rainbow) |
| `0` | Turn strip off |
| `Esc` | Quit the application (desde la bandeja) |
| `Arriba/Abajo` | Ajustar brillo en 5% (cuando lighting editor está visible) |

---

## 💡 Lighting Payload

El protocoleo REA determinó que `LightMode` es un enum internamente etiquetado
dentro de `Lighting`, por lo que el JSON enviado al daemon debe tener esta estructura:

```json
{
  "request": "set_lighting",
  "lighting": {
    "mode": {
      "mode": "off",     | "static",        | "effect"
    },
    "brightness": 80,     // 0-100
    "indicators": true    // gear LEDs behind the gears
  }
}
```

### Ejemplos completos:

```json
// Luz apagada
{"request": "set_lighting", "lighting": {"mode": {"mode": "off"}, "brightness": 0, "indicators": false}}

// Color estático (#FF0000 = rojo)
{"request": "set_lighting", "lighting": {"mode": {"mode": "static", "color": {"r": 255, "g": 0, "b": 0}}, "brightness": 100, "indicators": true}}

// Efecto breathing amber
{"request": "set_lighting", "lighting": {"mode": {"mode": "effect", "effect": 2}, "brightness": 80, "indicators": false}}

// Gradiente con brillo 50% e indicadores on
{"request": "set_lighting", "lighting": {"mode": {"mode": "effect", "effect": 4}, "brightness": 50, "indicators": true}}

// Error que da la app original si se usa formato plano:
// {"request": "set_lighting", "lighting": {"mode": "effect", "brightness": 80, "indicators": true}}
// → "invalid type: string "effect", expected internally tagged enum LightMode"
```

---

## ⚙️ Gear Presets

Las 4 gears almacenadas en el cooler con sus 3 niveles RPM cada una:

| Gear | RPM levels (oficiales) | Comentario |
|------|------------------------|------------|
| **Quiet** | 1300, 1700, 1900 | Nivel más bajo, menor ruido |
| **Standard** | 2100, 2400, 2700 | Uso diario balanceado |
| **Strong** | 2800, 3000, 3300 | Potencia media |
| **Overclock** | 3500, 3700, 4000 | Máximo rendimiento |

### Gating de supply:

- Cuando `supply=low` (cargar notebook), el firmware limita el máximo a 2700 rpm
- Con `supply=full` (cargar rápido USB-C), Strong y Overclock están disponibles
- El UI muestra automáticamente el ceiling correspondiente

### Disabled states:

Las RPM que exceden el supply_max_rpm reportado por el daemon se deshabilitan
visualmente (opacity 0.35) y muestran el motivo.

---

## 🏠 Resident Behaviour

El daemon `flydigictld` es el componente de bajo recurso que **posee el cooler**
aunque la ventana Qt esté oculta o cerrada. El GUI mantiene:

- **System tray entry** - Icono en la bandeja del sistema
- **Hide on close** - La ventana se oculta, no se cierra (la app sigue corriendo)
- **Restore from tray** - Click en el icono para mostrar la ventana nuevamente
- **Quit from tray** - "Quit" en el menú de la bandaje solo sale la GUI, no mata
  el daemon

### Smart Speed activation:

- **Startup**: Preserva el estado fijo del dispositivo (último gear seleccionado)
- **AUTO activation**: Smart Speed solo se activa después de seleccionar AUTO o
  una curva inteligente
- **Modelo de estado**: `smart_mode`, `fixed_gear`, y `manual` están separados así
  que un gear físico no se presenta como override manual RPM

---

## 📁 Project Structure

```
flydigi-bs2pro-linux/
├── CMakeLists.txt          # Build system
├── README.md               # This file
├── odd/                    # ODD task tracking
│   ├── tasks/space-station-gui.md
│   ├── tasks/official-cooling-modes.md
│   └── tasks/official-ui-parity.md
├── packaging/              # Distribución
│   ├── flydigi-bs2pro-linux.desktop
│   └── flydigi-bs2pro-linux.svg
├── qml/                    # Interfaz de usuario
│   ├── Controls.qml
│   ├── Main.qml
│   ├── Gauge.qml
│   ├── TempList.qml
│   ├── Theme.qml
│   ├── Pill.qml
│   └── gauge.qss
├── docs/                   # Documentation
│   └── screenshot.png
├── src/                    # Source C++
│   ├── backend.cpp
│   ├── backend.h
│   └── main.cpp
└── odd/                    # Task tracking (ODD)
```

---

## 👥 Credits

### Proyecto original:

- **flydigictl** - El daemon por el cual esta app se comunica.
  https://github.com/ElXreno/flydigictl

- **Flydigi BS2 Pro Control (Windows)** - La app oficial cuya inversa ingeniería
  inspiró este proyecto. Inspeccionada vía Wine + `innoextract` + extracción de
  `resources/app.asar`.

### Esta implementación:

- **ddoblejotadev** - Análisis REA, backend C++, QML UI, ODD tracking
- **Organic Driven Development** - Metodología de trabajo utilizada

### Herramientas usadas:

- **Wine + innoextract** - Extracción del instalador Windows
- **@electron/asar** - Desempaquetado del payload Electron
- **Qt 6.5+** - Framework para la GUI nativa
- **REA (Reverse Engineering Assistant)** - Metodología de análisis

---

## 📄 License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file
for details.

**Permissions and limitations under the MIT License:**

- ✅ Uso comercial y personal
- ✅ Modificación y distribución
- ✅ Inclusión en otros proyectos
- ❌ No responsabilizar autor por daños
- ❌ No remover el copyright/licencia

---

## 🛣 Roadmap (future)

| Prioridad | Feature | Descripción |
|-----------|---------|-------------|
| **Alta** | Persistencia de configuración | Guardar último socket, modo, brillo en QSettings |
| **Media** | Modo oscuro (Dark Theme) | QML `Theme.qml` dark variant |
| **Media** | Notificaciones toast | Mensajes emergentes para cambios de estado |
| **Baja** | Múltiples daemons | Soportar diferentes usuarios o contenedores |
| **Baja** | API REST | Wrapper HTTP sobre el socket local |

---

## 📞 Support

- **Issues**: Usa el tracker de GitHub del repositorio
- **Documentation**: Revisa el apartado `##` de este README
- **Daemon**: Reporta issues en https://github.com/ElXreno/flydigictl

---

*Hecho con ❤️ para la comunidad de entusiastas de cooling laptop.*