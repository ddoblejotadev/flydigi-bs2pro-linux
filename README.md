# Flydigi BS2 Pro Control for Linux

![GitHub license](https://img.shields.io/github/license/ddoblejotadev/flydigi-bs2pro-linux.svg)
![GitHub repo size](https://img.shields.io/github/repo-size/ddoblejotadev/flydigi-bs2pro-linux.svg)
![GitHub stars](https://img.shields.io/github/stars/ddoblejotadev/flydigi-bs2pro-linux.svg?style=social)
![GitHub last commit](https://img.shields.io/github/last-commit/ddoblejotadev/flydigi-bs2pro-linux.svg)
![GitHub actions](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/workflows/CI/badge.svg)

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
- [Getting Started](#getting-started)
- [Building from Source](#building-from-source)
- [Using Pre-built Releases](#using-pre-built-releases)
- [Usage](#usage)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Lighting Payload](#lighting-payload)
- [Gear Presets](#gear-presets)
- [Resident Behaviour](#resident-behaviour)
- [Troubleshooting](#troubleshooting)
- [Reporting Issues](#reporting-issues)
- [Credits](#credits)
- [License](#license)

---

## 🛬 Getting Started

### Prerequisites

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **Qt 6** | 6.5 | 6.6+ |
| **CMake** | 3.21 | 3.25+ |
| **OS** | Linux (any distro) | KDE/GNODE tested |
| **Daemon** | `flydigictl` v0.1+ | Latest from GitHub |

### Quick install (via releases)

1. **Download the latest release** from: https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases
2. **Extract** the tarball/`zip`
3. **Run** `./flydigi-bs2pro-linux` (no installation needed)
4. **Or** copy the binary to `~/bin/` and the `FlydigiSpace/` folder to `~/.local/share/`

### Verificar que el daemon corre

```bash
# Comprobar proceso
ps aux | grep flydigictl | grep -v grep

# Comprobar socket
ls -la /run/flydigictl/flydigictl.sock 2>/dev/null || \
ls -la $XDG_RUNTIME_DIR/flydigictl.sock 2>/dev/null || \
echo "Daemon socket not found"

# Si no hay socket, iniciar el daemon
# (usúa tu método: systemctl --user, servicio, o manual)
```

---

## 🏗 REA Methodology

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

| Hallazgo | Evidencia | Corrección |
|----------|-----------|------------|
| `LightMode` es enum interno etiquetado | L9: app enviaba `"mode":"effect"`, daemon lo rechazaba | Estructura anidada en `sendLighting()` |
| Estructura wire format anidada | L10: `"mode":{"mode":"effect","effect":N}` es el formato correcto | Implementado en `backend.cpp:sendLighting()` |
| Every lighting request rejected | L10: click mostraba cambio pero strip nunca respondía | `applyStatus()` ahora descifra respuesta anidada |

### Enlaces de referencia:

- **flydigictl repo**: https://github.com/ElXreno/flydigictl
- **Protool analysis**: L9-L10 ODD tasks `odd/tasks/official-cooling-modes.md`
- **Vendor vocabulary**: `locales/en/translation.json` from Windows installer

---

## 🏗 Architecture

```text
flydigi-bs2pro-linux/
├── .github/                # CI/CD workflows
│   └── workflows/
│       └── ci.yml          # Build & test pipeline
├── src/                    # Backend C++ (cliente daemon)
│   ├── backend.cpp         # Socket JSON, lighting protocol, status parsing
│   ├── backend.h         # Q_PROPERTY declarations, public API
│   └── main.cpp          # Entry point, Qt Application, system tray
├── qml/                    # Interfaz de usuario QML
│   ├── Controls.qml      # Gear presets, curve editor, lighting controls
│   ├── Main.qml          # Ventana principal, layout, tray integration
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
├── .github/                # GitHub Actions CI
│   └── workflows/
│       └── ci.yml          # Build & test pipeline
├── CMakeLists.txt          # Build system Qt 6
└── README.md               # This file
```

### Componentes principales:

| Componente | Responsabilidad |
|------------|-----------------|
| **Backend** | Cliente Unix domain socket, parsing de respuestas, envío de comandos |
| **QML** | UI rendering, user interaction, visual feedback |
| **Daemon** `flydigictld` | Propiedad del cooler, control real del hardware |
| **GitHub Actions** | CI pipeline: build, test, generate releases |
| **CMake** | Build system, instalación desktop entry + QML module |

### Decisiones de diseño:

- **Comunicación daemon vs app directa**: El daemon posee el cooler; la GUI solo
  consulta. Nunca se abre `/dev/hidraw2` directamente desde la aplicación.
- **Pacing de lighting 300ms**: Matching vendor client timing para que el daemon
  procese los reports sin perderlos.
- **Estructura JSON anidada**: `LightMode` es enum interno al `Lighting` struct,
  por lo que el wire format debe ser `{"mode":{"mode":"effect","effect":N}}`.
- **Consumo bajo**: Qt6 + QLocalSocket es eficiente; ~0.2% CPU quieto en bandeja.
- **CICLE (CI)**: Todo push a `main` dispara build y genera release automática.

---

## 📦 Building from Source

```sh
# 1. Clonar repo
git clone https://github.com/ddoblejotadev/flydigi-bs2pro-linux.git
cd flydigi-bs2pro-linux

# 2. Configurar y compilar
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# 3. Instalar (prefix por defecto es $HOME/.local)
cmake --install build --prefix "$HOME/.local"

# 4. Verificar
flydigi-bs2pro-linux --help
```

### Desarrollo rápido

```sh
# Modo debug
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

# Ejecutar sin instalar
./build/flydigi-bs2pro-linux
```

---

## 📦 Using Pre-built Releases

¡No necesitas compilar! Cada push a `main` dispara GitHub Actions que genera
un paquete con el binary listo para usar.

### Available releases:

| OS | Format | Download |
|----|--------|----------|
| **Linux (x86_64)** | `.tar.xz` | [Releases page](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases) |
| **Linux (aarch64)** | `.tar.xz` | [Releases page](https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases) |

### Install from release tarball

```sh
# Descargar y extraer
cd /tmp
curl -LJO https://github.com/ddoblejotadev/flydigi-bs2pro-linux/releases/download/v0.1.0/flydigi-bs2pro-linux-v0.1.0-linux-x86_64.tar.xz
tar xf flydigi-bs2pro-linux-v0.1.0-linux-x86_64.tar.xz

# Mover binario
mv flydigi-bs2pro-linux-v0.1.0/flydigi-bs2pro-linux ~/.local/bin/
mv flydigi-bs2pro-linux-v0.1.0/FlydigiSpace ~/.local/share/

# Mover desktop entry e icono
cp flydigi-bs2pro-linux-v0.1.0/flydigi-bs2pro-linux.desktop ~/.local/share/applications/
cp flydigi-bs2pro-linux-v0.1.0/flydigi-bs2pro-linux.svg ~/.local/share/icons/hicolor/scalable/apps/

# Ejecutar
flydigi-bs2pro-linux
```

### Versionado semántico

El proyecto usa versionado semántico. Las releases siguen el patrón `vMAJOR.MINOR.PATCH`:

- **v0.1.0**: Release inicial con GUI nativa, todos los modos S1-S9
- **v0.1.1**: Bugfixes de estabilidad, mejora de pacing
- **v0.2.0**: Nuevas características (persistencia, modo oscuro, etc.)

---

## 🛠 Usage

### Al iniciar:

1. Asegurarse de que `flydigictl` daemon esté corriendo
2. Ejecutar `flydigi-bs2pro-linux`
3. La ventana principal se abrirá y se minimizará automáticamente a la bandeja del sistema

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
- **Quit from tray** - "Quit" en el menú de la bandeja solo sale la GUI, no mata
  el daemon

### Smart Speed activation:

- **Startup**: Preserva el estado fijo del dispositivo (último gear seleccionado)
- **AUTO activation**: Smart Speed solo se activa después de seleccionar AUTO o
  una curva inteligente
- **Modelo de estado**: `smart_mode`, `fixed_gear`, y `manual` están separados así
  que un gear físico no se presenta como override manual RPM

---

## 🔧 Troubleshooting

### El programa no se conecta / "Daemon not found"

| Causa | Solución |
|-------|----------|
| `flydigictl` no está corriendo | Iniciar el daemon: `flydigictl -c ~/.config/flydigictl/config.toml` |
| Socket en ruta incorrecta | Pasar socket por parámetro: `./flydigi-bs2pro-linux --socket /ruta/al/socket` |
| Permisos del socket | Verificar: `ls -la /run/flydigictl/` o `$XDG_RUNTIME_DIR/` |
| Wayland/X11 issues | Probar con `XDG_RUNTIME_DIR=/run/user/$UID` |

### El programa se cierra inesperadamente

| Posible causa | Acción |
|---------------|--------|
| Daemon actualizó su API | Actualizar `flydigictl` a última versión |
| Archivo de configuración corrupto | `rm ~/.config/flydigictl/config.toml` y reiniciar |
| Colisión de puertos | Usar `--socket` con ruta personalizada |

### No hay audio / problemas de sonido

- Este programa **no reproduce audio**. Es un control de ventiladores.
- Si esperabas efectos de sonido, revisa la documentación del daemon `flydigictl`.

### Performance issues (alto consumo CPU/memoria)

| Síntoma | Causa probable | Fix |
|---------|----------------|-----|
| CPU > 10% quieto | Loop de reconecte intenso | Reiniciar el daemon, verificar `XDG_RUNTIME_DIR` |
| Memoria crece | Filtro de sensors sin limpiar | Actualizar a última release |
| Redraws QML lentos | Driver GPU viejo | Actualizar drivers, usar Qt 6.6+ |

### Comprobación rápida

```bash
# Verificar que todo está OK
./flydigi-bs2pro-linux --help 2>&1 | head -3

# Ver estado del daemon
pgrep -a flydigictl || echo "Daemon no corriendo"

# Ver socket
ls -la /run/flydigictl/flydigictl.sock 2>/dev/null || echo "Socket no encontrado"
```

---

## 🐛 Reporting Issues

### Antes de reportar

1. **Actualizar a la última release** - Muchos bugs ya fueron corregidos
2. **Verificar que el daemon corra** - `ps aux | grep flydigictl`
3. **Revisar la sección de Troubleshooting** - Posible solución conocida

### Cómo reportar

1. Ve a la pestaña **Issues** del repositorio: https://github.com/ddoblejotadev/flydigi-bs2pro-linux/issues
2. Haz clic en **"New issue"**
3. Selecciona la plantilla apropiada:
   - **Bug report** - Si algo no funciona como debería
   - **Feature request** - Si querés una nueva funcionalidad
   - **Question** - Si tenés dudas de uso

### Plantilla mínima para Bug report

```markdown
## Descripción
Descripción clara y concisa de qué es el bug.

## Versión
- Versión del programa: v0.1.0 (ver `./flydigi-bs2pro-linux --version`)
- Versión del daemon `flydigictl`: v0.1+

## Entorno
- SO: Linux (distro y versión)
- Escritorio: KDE / GNOME / XFCE / Wayland / X11
- Output de `uname -a`

## Pasos para reproducir
1. Hacer esto
2. Hacer aquello
3. Observar el error

## Comportamiento esperado
¿Qué deberías haber pasado?

## Registers (opcional)
Output de `./flydigi-bs2pro-linux 2>&1` cuando falla el tema.
Output de `journalctl -u flydigictl` si es relevante.
```

### Información útil para los maintainers

Al reportar un issue, incluye siempre:

| Dato | Cómo obtenerlo |
|------|----------------|
| **Versión del programa** | `./flydigi-bs2pro-linux --version` (si está disponible) o revisar el binary |
| **Versión del daemon** | `flydigictl --version` o revisar el fuente |
| **Output del terminal** | `./flydigi-bs2pro-linux 2>&1` captando la salida |
| **Estado del daemon** | `ps aux | grep flydigictl` |
| **Socket en uso** | `ls -la $XDG_RUNTIME_DIR/flydigictl.sock` |
| **Errores en consola** | Ejecutar con `./flydigi-bs2pro-linux 2>&1` |

---

## 👥 Credits

### Proyecto original:

- **flydigictl** - El daemon por el cual esta app se comunica.
  https://github.com/ElXreno/flydigictl

- **Flydigi BS2 Pro Control (Windows)** - La app oficial cuya inversa ingeniería
  inspiró este proyecto. Inspeccionada vía Wine + `innoextract` + extracción de
  `resources/app.asar`.

### Esta implementación:

- **ddoblejotadev** - Análisis REA, backend C++, QML UI, ODD tracking, CI/CD
- **Organic Driven Development** - Metodología de trabajo utilizada
- **GitHub Actions** - Integración continua y despliegue de releases

### Herramientas usadas:

- **Wine + innoextract** - Extracción del instalador Windows
- **@electron/asar** - Desempaquetado del payload Electron
- **Qt 6.5+** - Framework para la GUI nativa
- **REA (Reverse Engineering Assistant)** - Metodología de análisis
- **GitHub Actions** - CI/CD pipeline para builds y releases

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

## 🛣 Roadmap

| Versión | Características |
|---------|-----------------|
| **v0.1.0** (actual) | GUI nativa, todos los modos S1-S9, pacing lighting, gears, standby |
| **v0.1.1** | Bugfixes de estabilidad, mejora de logging, mejor manejo de errores |
| **v0.2.0** | Persistencia de configuración (QSettings), modo oscuro, notificaciones toast |
| **v1.0.0** | Multi-plataforma (Windows/macOS), API REST, integración con más daemons |

Roadmap visible en el repositorio: https://github.com/ddoblejotadev/flydigi-bs2pro-linux/projects

---

## 📞 Contacto

- **Issues**: https://github.com/ddoblejotadev/flydigi-bs2pro-linux/issues
- **Daemon support**: https://github.com/ElXreno/flydigictl
- **Discussion**: https://github.com/ddoblejotadev/flydigi-bs2pro-linux/discussions

---

Hecho con ❤️ para la comunidad de entusiastas de cooling laptop.