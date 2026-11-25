# VideoLab: del código fuente a la aplicación funcionando

Esta guía explica todo el proceso: qué se necesita instalar, cómo se genera el *build* con CMake y vcpkg, cómo se compila en Visual Studio, qué pasa por dentro cuando la aplicación arranca y cómo fluyen el video y el audio hasta el archivo exportado.

| Dato | Valor |
|---|---|
| Lenguaje | C++17 |
| Interfaz | MFC (Microsoft Foundation Classes) |
| Multimedia | FFmpeg 9.0.2 (libavformat, libavcodec, libswscale, libswresample) |
| Audio en tiempo real | WinMM (`waveOut`) |
| Sistema de build | CMake 3.20+ con presets |
| Gestor de bibliotecas | vcpkg (modo manifiesto) |
| IDE y compilador | Visual Studio 2026 (versión 18), MSVC x64 |
| Repositorio | https://github.com/Ramsi-rva/VideoLab |

---

## 1. Requisitos

1. **Visual Studio 2026 (versión 18)** con la carga de trabajo **"Desarrollo para el escritorio con C++"**.
2. El componente individual **"C++ MFC para las herramientas de compilación más recientes (x86 y x64)"**.
   En *Visual Studio Installer → Modificar → Componentes individuales*, busca "MFC". Sin este componente la compilación falla con `error MSB8041: Este proyecto requiere la instalación de bibliotecas MFC`.
3. **CMake** 3.20 o superior (Visual Studio ya trae uno; también se instala con `winget install Kitware.CMake`).
4. **Git** (https://git-scm.com).
5. **vcpkg**, el gestor de bibliotecas de C++ de Microsoft. Se instala una sola vez:

```powershell
cd C:\
git clone https://github.com/microsoft/vcpkg.git
C:\vcpkg\bootstrap-vcpkg.bat
```

> **Importante:** coloca el proyecto en una ruta **sin espacios**, por ejemplo `C:\dev\VideoLab` o `C:\Users\User\Documents\GitHub\VideoLab`. vcpkg compila FFmpeg con herramientas que fallan si la ruta tiene espacios (por ejemplo, `C:\Documents\Programacion del Sistema\...`).

---

## 2. Obtener el código

```powershell
mkdir C:\dev
cd C:\dev
git clone https://github.com/Ramsi-rva/VideoLab.git
cd VideoLab
```

Estructura del proyecto:

```
VideoLab/
├── CMakeLists.txt        Qué se compila y con qué bibliotecas se enlaza
├── CMakePresets.json     Configuración lista: generador VS 18, x64, toolchain de vcpkg
├── vcpkg.json            Dependencias: FFmpeg con avcodec, avformat, swscale, swresample
├── README.md
├── docs/GUIA_BUILD.md    (esta guía)
├── scripts/publish-github.ps1
└── src/
    ├── main.cpp          Aplicación MFC (CVideoLabApp), ventana (CMainFrame), páginas y línea de tiempo
    ├── Widgets.h/.cpp    Controles MFC propios: CVLWidget y CVLPopup (derivados de CWnd)
    ├── Theme.h           Colores y primitivas de dibujo sobre CDC (CPen, CBrush, CFont)
    ├── Project.h         Modelo: medios, clips de video, textos, clips de audio
    ├── MediaSource.*     Decodificación de video con FFmpeg
    ├── AudioSource.*     Decodificación de audio a PCM con FFmpeg + picos para la forma de onda
    ├── AudioMix.*        Mezcla de las pistas A1 y A2 (volumen, fundidos, silencio)
    ├── AudioEngine.*     Reproducción de sonido con waveOut
    ├── Renderer.*        Composición del cuadro final: ajustes, filtros, transiciones y textos
    ├── Scopes.*          Monitores de color: forma de onda, Parade RGB, histograma, vectorscopio
    └── Exporter.*        Codificación a MP4 (video MPEG-4 + audio AAC) con FFmpeg
```

---

## 3. Configurar el build (CMake + vcpkg)

```powershell
cd C:\dev\VideoLab
$env:VCPKG_ROOT = "C:\vcpkg"
cmake --preset windows-x64
```

Qué ocurre en este paso:

1. **CMake lee `CMakePresets.json`.** El preset `windows-x64` indica:
   - generador `Visual Studio 18 2026` y arquitectura `x64`;
   - carpeta de salida `build/`;
   - toolchain `%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake` y triplet `x64-windows` (DLL dinámicas).
2. **vcpkg lee `vcpkg.json`** e instala FFmpeg con solo las partes necesarias (`avcodec`, `avformat`, `swscale`, `swresample`). La primera vez **compila FFmpeg 9.0.2 desde el código fuente** (puede tardar entre 10 y 30 minutos). Las siguientes veces lo restaura de su caché (`%LOCALAPPDATA%\vcpkg\archives`) en segundos. Todo queda en `build\vcpkg_installed\x64-windows\`.
3. **CMake procesa `CMakeLists.txt`:**
   - `set(CMAKE_MFC_FLAG 2)` activa MFC como DLL compartida (en Visual Studio aparece como *"Usar MFC en un archivo DLL compartido"*);
   - `find_package(FFMPEG REQUIRED)` localiza las cabeceras y bibliotecas `.lib` de FFmpeg que instaló vcpkg;
   - define `UNICODE`/`_UNICODE` (la aplicación usa texto ancho) y las opciones `/W3 /utf-8`.
4. **Genera la solución de Visual Studio** en `build\` (`VideoLab.sln`/`.slnx` y `VideoLab.vcxproj`).

Salida esperada (resumida):

```
ffmpeg[avcodec,avformat,core,swresample,swscale]:x64-windows@9.0.2
-- The CXX compiler identification is MSVC 19.5x...
-- Check for working CXX compiler: C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/.../cl.exe
-- Found FFMPEG: ...avformat.lib;...avcodec.lib;...swresample.lib;...swscale.lib;...avutil.lib
-- Build files have been written to: C:/dev/VideoLab/build
```

> Comprueba que el compilador sea el de **Visual Studio 18** (`MSVC 19.5x`, ruta `...\18\Community\...`). Si aparece `2022\BuildTools` (`MSVC 19.44`), CMake tomó otra instalación que no tiene MFC.
>
> Los avisos `CMake Warning (dev) ... CMP0174` vienen del módulo de FFmpeg de vcpkg y se pueden ignorar.

---

## 4. Compilar

Desde la terminal:

```powershell
cmake --build build --config Release
```

O desde Visual Studio: abre `build\VideoLab.sln`, haz clic derecho en el proyecto **VideoLab** → *Establecer como proyecto de inicio*, elige la configuración **Release** y pulsa **F5**.

Qué ocurre:

1. **MSBuild** compila cada `.cpp` con `cl.exe`. Como todos los archivos incluyen `Util.h`, que incluye `<afxwin.h>` y `<afxdlgs.h>`, se compilan con las cabeceras de MFC.
2. **El enlazador** (`link.exe`) une:
   - los `.obj` del proyecto;
   - las bibliotecas de importación de FFmpeg (`avformat.lib`, `avcodec.lib`, `swscale.lib`, `swresample.lib`, `avutil.lib`);
   - MFC (`mfc140u.lib`, que se agrega automáticamente por usar MFC);
   - bibliotecas de Windows: `winmm` (audio), `gdi32` (dibujo), `comdlg32`, `shell32`, `dwmapi`, etc.
   MFC aporta su propio punto de entrada (`wWinMain`), por eso en el código no hay `WinMain`.
3. **vcpkg copia las DLL de FFmpeg** (`avcodec-*.dll`, `avformat-*.dll`, `avutil-*.dll`, `swscale-*.dll`, `swresample-*.dll`) junto al ejecutable, para que arranque sin configurar nada más.

Resultado: `build\Release\VideoLab.exe`.

---

## 5. Ejecutar

```powershell
.\build\Release\VideoLab.exe
```

La barra de título muestra **"MFC • FFmpeg 9.0.2 • 30 fps"**. La versión de FFmpeg se lee en tiempo real con `av_version_info()`, así que confirma con qué FFmpeg se enlazó el programa.

---

## 6. Qué pasa por dentro al arrancar (MFC)

```
theApp (objeto global CVideoLabApp : CWinApp)
   │  MFC llama a wWinMain → AfxWinMain
   ▼
CVideoLabApp::InitInstance()
   │  crea CMainFrame y llama a Create(...)
   ▼
CMainFrame::PreCreateWindow()   registra la clase de ventana (AfxRegisterWndClass, doble clic)
CMainFrame::OnCreate()          (ON_WM_CREATE)
   ├─ crea las fuentes (CFont) y el pincel del cuadro de texto (CBrush)
   ├─ activa la barra de título oscura (DwmSetWindowAttribute)
   ├─ crea los lienzos de imagen (vista previa, fuente y scopes)
   ├─ crea los controles: CVLWidget (botones, sliders, ruedas, desplegables) y CEdit
   ├─ DragAcceptFiles(TRUE)  → permite arrastrar archivos a la ventana
   └─ layoutAll()            → coloca cada control según la página activa
   ▼
Bucle de mensajes de MFC (CWinApp::Run)
   └─ cada mensaje de Windows se envía al método indicado en el mapa de mensajes
```

### Mapa de mensajes de la ventana principal

`BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)` conecta cada mensaje con su método:

| Mensaje | Método | Para qué |
|---|---|---|
| `ON_WM_PAINT` | `OnPaint` | Dibuja la página con doble búfer (`CDC` + `CBitmap` en memoria) |
| `ON_WM_SIZE` | `OnSize` | Recoloca los controles al cambiar el tamaño |
| `ON_WM_LBUTTONDOWN` / `DBLCLK` | `OnLButtonDown` / `OnLButtonDblClk` | Pestañas de página, línea de tiempo, galería de medios, visor |
| `ON_WM_MOUSEMOVE` / `ON_WM_LBUTTONUP` | `OnMouseMove` / `OnLButtonUp` | Arrastrar y recortar clips |
| `ON_WM_MOUSEWHEEL` | `OnMouseWheel` | Desplazamiento y zoom (Ctrl) de la línea de tiempo |
| `ON_WM_KEYDOWN` | `OnKeyDown` | Atajos de teclado |
| `ON_WM_TIMER` | `OnTimer` | Avanza la reproducción y alimenta el audio |
| `ON_WM_DROPFILES` | `OnDropFiles` | Importar archivos arrastrados |
| `ON_MESSAGE(WM_WIDGET, ...)` | `OnWidgetMsg` | Avisos de los controles propios |
| `ON_MESSAGE(WM_EXPORT_PROGRESS / DONE, ...)` | `OnExportProgress` / `OnExportDone` | Progreso y fin de la exportación |
| `ON_EN_CHANGE` | `OnTextChange` | Edición del texto de un título (CEdit) |
| `ON_WM_CTLCOLOR` | `OnCtlColor` | Colores oscuros del CEdit |
| `ON_WM_CLOSE` / `ON_WM_DESTROY` | `OnClose` / `OnDestroy` | Confirmar salida y liberar recursos |

### Controles propios (MFC)

Cada botón, deslizador, interruptor, rueda de color y desplegable es un **`CVLWidget`**, una clase derivada de **`CWnd`** con su propio mapa de mensajes (`ON_WM_PAINT`, `ON_WM_LBUTTONDOWN`, `ON_WM_MOUSEMOVE`, `ON_WM_MOUSELEAVE`, `ON_WM_CAPTURECHANGED`…). Al cambiar de valor envía `WM_WIDGET` a la ventana principal (`GetParent()->SendMessage(WM_WIDGET, id, evento)`). La lista de un desplegable es un **`CVLPopup`**, una ventana emergente que se borra sola en `PostNcDestroy`.

Se hicieron controles propios, en lugar de `CButton` o `CSliderCtrl`, para lograr el tema oscuro al estilo DaVinci Resolve, algo que los controles estándar de Windows no permiten.

---

## 7. Funcionamiento: del archivo de entrada al MP4 final

### 7.1 Importar (página Medios)

*Importar medios*, Ctrl+I o arrastrar archivos a la ventana:

1. `probeStreams()` abre el archivo con `avformat_open_input` y detecta si tiene video y/o audio.
2. **Video:** `MediaSource` abre el decodificador (`avcodec_open2`), lee la duración y los fps, y genera una miniatura.
3. **Audio:** `AudioSource` decodifica todo el audio a PCM de 16 bits, estéreo, 44 100 Hz (con `swr_convert` de libswresample) y calcula los picos para dibujar la forma de onda.

### 7.2 Editar (página Edición)

- **+ Video** coloca el clip en la pista V1; **+ Audio** lo coloca en A1 o A2. El texto se agrega como título.
- Los clips se mueven y recortan arrastrando sus bordes; **S** o **Ctrl+B** divide en el cabezal; **Supr** borra.
- El inspector cambia la velocidad (0.25x a 4x), el zoom y la posición, el filtro (escala de grises, bordes Sobel, invertir colores), la transición (fundido a negro), y en el audio el volumen, la entrada, la salida y el silencio.
- Cada cambio guarda una copia del proyecto para **deshacer/rehacer** (Ctrl+Z / Ctrl+Y).

### 7.3 Color (página Color)

Ruedas de color **Sombras (Lift)**, **Medios (Gamma)** y **Luces (Gain)**, además de brillo, contraste, saturación, temperatura y tinte. `Renderer` construye tablas de búsqueda (LUT) y las aplica a cada pixel. Los **scopes** (`Scopes.cpp`) analizan el cuadro mostrado: forma de onda, Parade RGB, histograma y vectorscopio.

### 7.4 Reproducir

**Espacio** o **L** reproduce y **K** detiene:

1. `AudioEngine::start()` abre `waveOut` y prepara 6 búferes de 2048 muestras.
2. Cada 15 ms el temporizador (`OnTimer`) llama a `pump()`, que rellena los búferes libres con `mixAudio()` (suma de A1 + A2 con volumen y fundidos).
3. La posición real del audio (`waveOutGetPosition`) mueve el cabezal, así la imagen queda sincronizada con el sonido.
4. `Renderer` compone el cuadro de ese instante (decodifica el video con `MediaSource`, aplica color, filtro, transición y textos) y el visor lo dibuja con `CDC::StretchBlt`.

### 7.5 Exportar (página Entrega)

Se elige la resolución (480p, HD o Full HD), los fotogramas (24, 25, 30 o 60 fps), la calidad (baja, media o alta) y el archivo de salida, y se pulsa **Renderizar**:

```
Hilo de exportación (std::thread)
  para cada cuadro i:
     Renderer.render(proyecto, i / fps)  → imagen BGRA
     sws_scale  BGRA → YUV420P           (libswscale)
     avcodec_send_frame / receive_packet  (encoder MPEG-4)
     mixAudio → AAC 192 kbps estéreo     (encoder AAC)
     av_interleaved_write_frame          (contenedor MP4)
     PostMessage(WM_EXPORT_PROGRESS)     → barra de progreso en la ventana
  av_write_trailer → PostMessage(WM_EXPORT_DONE)
```

La exportación corre en un hilo aparte para que la interfaz no se congele. Ese hilo solo usa el *handle* de la ventana para enviar mensajes (`::PostMessage`), porque los objetos de MFC no deben compartirse entre hilos. El botón cambia a **Cancelar** mientras dura.

### Atajos de teclado

| Tecla | Acción |
|---|---|
| Espacio / L | Reproducir / pausar |
| K | Detener |
| ← / → | Cuadro anterior / siguiente (Shift: 1 segundo) |
| ↑ / ↓ | Corte de edición anterior / siguiente |
| Inicio / Fin | Ir al principio / al final |
| S, Ctrl+B | Dividir clip |
| Supr | Borrar selección |
| Ctrl+Z / Ctrl+Y | Deshacer / rehacer |
| Ctrl+I | Importar |
| Ctrl+E | Ir a Entrega |
| Shift+2 / 4 / 6 / 8 | Páginas Medios / Edición / Color / Entrega |

---

## 8. Cómo comprobar que es una aplicación MFC

1. **En la app:** la barra de título dice "MFC • FFmpeg 9.0.2".
2. **En el código:** `class CVideoLabApp : public CWinApp`, `BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)` y `class CVLWidget : public CWnd`.
3. **En el proyecto:** Propiedades → Avanzado → *Uso de MFC: Usar MFC en un archivo DLL compartido* (viene de `CMAKE_MFC_FLAG 2`).
4. **En el ejecutable**, desde "Developer PowerShell for VS":

```powershell
dumpbin /dependents build\Release\VideoLab.exe
```

En la lista aparece `mfc140u.dll` junto a las DLL de FFmpeg.

---

## 9. Problemas encontrados y su solución

| Error | Causa | Solución |
|---|---|---|
| `CMakeCache.txt directory ... is different than the directory ... where CMakeCache.txt was created` | Se copió una carpeta `build` generada en otra ruta | Borrar `build` y volver a ejecutar `cmake --preset windows-x64` |
| `could not find specified instance of Visual Studio ... 18/Community` | El preset decía VS 2022 y el equipo tiene VS 18 | El preset usa `"generator": "Visual Studio 18 2026"` |
| `MSB8013: ... no contiene la combinación ... Releasem\|x64` | Error al escribir el comando | Usar `--config Release` |
| `MSB8041: Este proyecto requiere la instalación de bibliotecas MFC` | Se usó una instalación sin el componente MFC (Build Tools 2022) | Instalar "C++ MFC..." o asegurarse de usar VS 18 |
| `LNK2001: símbolo externo wWinMainCRT / __std_terminate / memcpy sin resolver` | Opciones de enlace manuales (`/ENTRY:wWinMainCRT` y `_AFXDLL`) que chocaban con lo que ya configura Visual Studio al usar MFC | Se quitaron: `CMAKE_MFC_FLAG 2` basta |
| `build is not a directory` | Se borró la carpeta `build` | Volver a ejecutar `cmake --preset windows-x64` |
| vcpkg falla al compilar FFmpeg | Ruta con espacios | Usar `C:\dev\VideoLab` |

---

## 10. Publicar cambios en GitHub

```powershell
cd C:\dev\VideoLab
git add .
git commit -m "Descripción del cambio"
git push
```

La carpeta `build` (FFmpeg compilado y el `.exe`) no se sube: está en `.gitignore`. Cualquier persona que clone el repositorio la regenera con los pasos 3 y 4.

---

## Resumen en 5 comandos

```powershell
git clone https://github.com/Ramsi-rva/VideoLab.git C:\dev\VideoLab
cd C:\dev\VideoLab
$env:VCPKG_ROOT = "C:\vcpkg"
cmake --preset windows-x64
cmake --build build --config Release; .\build\Release\VideoLab.exe
```
