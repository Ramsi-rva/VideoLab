# VideoLab - Editor de video (estilo DaVinci Resolve simplificado)

Editor de video y audio de escritorio en C++17 con **MFC** (CWinApp, CFrameWnd, controles CWnd propios con mapa de mensajes y dibujo con CDC), WinMM y **FFmpeg 9**. Interfaz oscura organizada por **paginas**,
como DaVinci Resolve.

## Paginas (barra inferior)
| Pagina | Atajo | Que hace |
|---|---|---|
| **Medios** | Shift+2 | Pool de medios con miniaturas, vista previa del archivo y metadatos |
| **Edicion** | Shift+4 | Pool + visor + inspector arriba; linea de tiempo con pistas V1 (video), T1 (titulos), A1 y A2 (audio) |
| **Color** | Shift+6 | Ruedas Lift/Gamma/Gain, primarios, scopes en vivo y tira de clips |
| **Entrega** | Shift+8 | Resolucion, fotogramas, calidad, archivo de salida y cola de render con progreso |

## Funciones
- **Importar** videos y audios (boton, `Ctrl+I` o arrastrar archivos); doble clic en un medio lo agrega a la linea de tiempo.
- **Linea de tiempo:** cabeceras de pista, regla, cabezal, zoom, miniaturas en los clips.
  Dividir (`S`), eliminar (`Supr`), recortar arrastrando los bordes, reordenar arrastrando el clip.
- **Inspector (clip):** velocidad (0.25x-4x), zoom y posicion X/Y, brillo, contraste, saturacion, filtros (grises, Sobel, invertir), fundido a negro.
- **Audio:** importa `mp3, wav, aac, flac, ogg, m4a, wma, opus` (o el sonido de un video con **+ Audio**). Un audio suelto
  va directo a la pista A1 (o A2 si A1 esta ocupada) con su **forma de onda**. Se puede mover (tambien entre A1 y A2),
  recortar por los bordes, dividir (`S`) y eliminar. El inspector da **volumen** (0-200 %), **fundido de entrada y de salida**
  y **silenciar**. Suena durante la reproduccion (sincronizado con el video) con medidor de nivel L/R bajo el codigo de tiempo.
- **Titulos:** texto, tamano, posicion, color y fondo oscuro; se mueven y alargan en la pista T1.
- **Color primario:** ruedas Lift (sombras), Gamma (medios) y Gain (luces) con control maestro (Y),
  temperatura y tinte. Doble clic en una rueda o en su control Y para restablecerla.
- **Scopes:** forma de onda, parade RGB, histograma y vectorscopio (con objetivos de color y linea de piel).
- **Deshacer / rehacer** ilimitado hasta 100 pasos.
- **Exportar a MP4** (video MPEG-4 + audio AAC 192 kbps estereo, con todas las pistas mezcladas) en 480p / 720p / 1080p, 24-60 fps, con calidad baja/media/alta y cancelacion.

## Atajos
| Tecla | Accion |
|---|---|
| Espacio / L | Reproducir / pausar |
| K | Pausar |
| S o Ctrl+B | Dividir clip en el cabezal |
| Supr | Eliminar clip o titulo seleccionado |
| Izquierda / Derecha | Un fotograma (con Shift: 1 s) |
| Arriba / Abajo | Punto de edicion anterior / siguiente |
| Inicio / Fin | Ir al principio / al final |
| Ctrl+Z / Ctrl+Y | Deshacer / rehacer |
| Ctrl+I | Importar |
| Ctrl+E | Ir a la pagina Entrega |
| Rueda del raton (linea de tiempo) | Desplazar; con Ctrl, zoom |

## Estructura (MVC)
| Capa | Archivos |
|---|---|
| Modelo | `Project.h` (clips, audio, titulos, color), `MediaSource.*` (video con FFmpeg), `AudioSource.*` (audio con FFmpeg + swresample a PCM 44.1 kHz), `AudioMix.*` (mezcla de pistas), `Renderer.*` (compone cada frame y aplica el color), `Scopes.*`, `Exporter.*` (codifica MP4) |
| Vista | `Theme.h` (primitivas de dibujo sobre `CDC` con `CPen`, `CBrush`, `CFont`), `Widgets.*` (`CVLWidget` y `CVLPopup`, controles MFC derivados de `CWnd` con su propio mapa de mensajes), `AudioEngine.*` (salida de sonido con waveOut), `paint` y `layoutAll` en `main.cpp` |
| Controlador | `CMainFrame` (CFrameWnd con `BEGIN_MESSAGE_MAP`), `CVideoLabApp` (CWinApp), `onWidget` y manejadores de raton/teclado en `main.cpp` |

## Requisitos
- Visual Studio 2026 (version 18) con la carga "Desarrollo de escritorio con C++" **y el componente "C++ MFC para las herramientas de compilacion mas recientes (x86 y x64)"** (Visual Studio Installer > Componentes individuales).
- CMake y vcpkg. vcpkg instala la version actual de FFmpeg (9.x); la version enlazada se muestra en la barra de titulo de la app (`av_version_info()`).
- La version anterior en Win32 puro esta en `VideoLab_Win32.zip`.

## Compilar (Visual Studio 2026 + vcpkg + CMake)
```bat
set VCPKG_ROOT=C:\vcpkg
cmake --preset windows-x64
cmake --build --preset release
build\Release\VideoLab.exe
```
Si actualizas desde una version sin audio, vuelve a ejecutar `cmake --preset windows-x64` (vcpkg recompila FFmpeg con `swresample`, unos minutos).
Usa una ruta del proyecto sin espacios (por ejemplo `C:\dev\VideoLab`) para que vcpkg compile FFmpeg sin problemas.

## Limitaciones
El audio de un video no se agrega solo: usa **+ Audio** para llevarlo a A1/A2 (el audio no esta enlazado al clip de video). Un carril de video, formato de salida 16:9, correccion de color primaria (sin nodos ni curvas)
y sin guardar proyectos todavia.


## Publicar en GitHub
Con Git y GitHub CLI instalados: `.\scripts\publish-github.ps1 -Name VideoLab -Visibility public`
