# Smart-Meter-Firmware-TPP

Firmware del medidor inteligente — Trabajo Práctico Profesional (FIUBA).

Target: **STM32L031K6** (Nucleo-L031K6) + modem **Quectel BG95** (NB-IoT).

---

## Setup

### 1. Instalar STM32CubeCLT

Bajarlo de [st.com/en/development-tools/stm32cubeclt.html](https://www.st.com/en/development-tools/stm32cubeclt.html) (gratis, requiere registro). Incluye toolchain ARM, ST-LINK GDB server, STM32CubeProgrammer y archivos SVD.

### 2. Configurar `STM32CubeCLT_PATH` y agregar binarios al PATH

`tasks.json` resuelve las herramientas vía la env var `STM32CubeCLT_PATH`. Además, el toolchain (`arm-none-eabi-gcc`) tiene que estar en PATH para que `cmake` lo encuentre.

- **Windows**: el instalador setea `STM32CubeCLT_PATH` automáticamente y agrega los bins al PATH del sistema. Verificar en PowerShell:
  ```powershell
  echo $env:STM32CubeCLT_PATH
  arm-none-eabi-gcc --version
  ```

- **Linux**: agregar a `~/.bashrc` (o `~/.zshrc`):
  ```bash
  export STM32CubeCLT_PATH=/opt/st/stm32cubeclt_1.19.0
  export PATH="$STM32CubeCLT_PATH/GNU-tools-for-STM32/bin:$STM32CubeCLT_PATH/STM32CubeProgrammer/bin:$PATH"
  ```
  Ajustar la versión a la instalada. Reabrir terminal y VS Code después.

- **macOS**: igual que Linux pero el path típico es `/opt/ST/STM32CubeCLT_1.19.0`.

### 3. Instalar VS Code y extensiones

Las que están listadas en `SM_Project/.vscode/extensions.json`:
- **CMake Tools** (`ms-vscode.cmake-tools`)
- **Cortex-Debug** (`marus25.cortex-debug`)
- **C/C++** (`ms-vscode.cpptools`)

### 4. (Opcional, Linux) Permisos para ST-LINK sin sudo

```bash
sudo cp /opt/st/stm32cubeclt_*/STLink-gdb-server/bin/*.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```
Desenchufar y volver a enchufar el ST-LINK.

---

## Compilar

Abrir `SM_Project/` en VS Code — CMake Tools detecta `CMakePresets.json`. Seleccionar el preset **Debug** y dar Build (`Ctrl+Shift+B`).

Equivalente desde terminal (dentro de `SM_Project/`):
```bash
cmake --preset Debug
cmake --build --preset Debug
```

El binario queda en `build/Debug/Smart-Meter-FW-TPP.elf`.

## Debuguear

Con la placa conectada vía ST-LINK, ir a **Run and Debug** (`Ctrl+Shift+D`), elegir una config y `F5`:

- **Debug w/ ST-Link** — flashea y debuga (corre `Build + Flash` antes).
- **Attach w/ ST-Link** — se conecta sin flashear.
- **OpenOCD** — alternativa (requiere `openocd` en PATH).

---

## Documentación (Doxygen)

La documentación del firmware se genera con [Doxygen](https://www.doxygen.nl) a partir de los comentarios del código y vive en `Doc/`. La documentación del código está **en inglés**. Usa el tema [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css), ya incluido en el repo (no hace falta instalarlo).

**Requisitos:**
- Doxygen ≥ 1.9.5
- Graphviz (`dot`) — para los grafos de llamadas y colaboración

**Generar:**
```bash
cd Doc
doxygen Doxyfile
```

Abrir `Doc/html/index.html` en el navegador. La página principal (resumen, arquitectura y máquinas de estado) está definida en `Doc/mainpage.dox`.

---

## Estructura del repo

```
Smart-Meter-Firmware-TPP/
├── SM_Project/              # Proyecto CubeMX/CMake principal
│   ├── Core/                # Código de aplicación (módulos + main)
│   ├── Drivers/             # HAL/CMSIS de STMicro
│   ├── cmake/               # Toolchain files
│   └── .vscode/             # Configs de build y debug
└── Doc/                     # Documentación Doxygen + datasheets
```
