# Escala fraccionaria (HiDPI) en VitruvianOS: análisis estático de arquitectura

Fecha: 2026-11-08. Alcance: `src/servers/app/`, `src/kits/interface/` y el puente
Wayland (`src/servers/app/WaylandWindow.*`, `src/add-ons/wlroots-koan/server/`).
Método: lectura estática del código con el vocabulario de *deep modules*
(módulo, interfaz, seam, adapter; skill `codebase-design`; la skill
`improve-codebase-architecture` no estaba disponible). **No se ha compilado ni
ejecutado nada.** Los números de línea corresponden al árbol de trabajo en esa
fecha. `koan_server.c` tiene cambios sin commitear (+13 líneas) de otra sesión,
así que sus números pueden no coincidir con `HEAD`.

Convención de evidencia:

- **[V]**: verificado leyendo el código citado. Si hay un ejemplo numérico, sale
  de aplicar a mano las fórmulas del propio código.
- **[S]**: suposición o inferencia que no se ha comprobado en ejecución ni se ha
  trazado del todo.

---

## 1. Resumen

Ya existe una base de escala por pantalla: el factor vive en `Screen`, se
persiste en `ScreenConfigurations`, se aplica en `Painter` como una transformación
AGG y se invierte para el puntero en `MouseFilter`. La arquitectura actual
**escala en espacio de pantalla y redondea región a región**, y eso choca con
la mayoría de los requisitos de `AGENTS.md`:

1. La transformación de vista del usuario y la escala se componen en **orden
   inverso** (bug de corrección para `BView::SetTransform`) [V].
2. Las regiones de clipping y de damage usan el **mismo redondeo expansivo**
   (`floor`/`ceil`). Dos rectángulos lógicos adyacentes **se solapan en una
   columna física**, así que hay overdraw entre ventanas y el hit-test no
   coincide con el píxel pintado [V].
3. La fase subpíxel del contenido depende de la posición de la ventana, porque
   la escala se aplica sobre el origen de la pantalla. El *blit* de
   `MoveWindowBy`/`CopyRegion` usa `round(dx*s)` y no reproduce lo que daría
   un repintado [V].
4. Con `s ≠ 1` se desactivan el alineado a píxel, el texto con hinting y el
   blit de bitmaps sin escalar. Todo pasa por las rutas vectoriales/afines, con
   resultado borroso y más lento [V].
5. Hay dos fuentes de "escala" que no se conocen entre sí: la heurística
   `font size / 12` (decoradores, cursores, `BControlLook`) y la escala por
   pantalla [V].
6. Algunas APIs con efectos en píxeles físicos reciben geometría lógica:
   `BScreen::GetBitmap`/`ReadBitmap` y `BDirectWindow` [V].
7. El puente Wayland no propaga la escala: `koan_server_set_output_dimensions()`
   es un stub sin llamadas, y `fractional-scale-v1`/`viewporter` se compilan
   pero no se instancian [V].

La oportunidad central es un **módulo profundo de geometría de salida**
(`OutputTransform`): la escala como racional `n/120`, el ajuste de *bordes* (no
de rectángulos) que conserva particiones, y variantes separadas para partición
(clipping/hit-test) y cobertura (damage). Las demás oportunidades se apoyan en
ese seam.

---

## 2. Vocabulario de espacios de coordenadas

El código usa "logical" con **dos significados** [V]:

| Término propuesto | Significado | Dónde se usa hoy |
|---|---|---|
| **Coordenadas lógicas** (BeAPI) | Píxeles físicos ÷ escala. Es lo que ven `BWindow`, `BScreen::Frame()`, `screen_where` | `Screen::LogicalFrame()` `Screen.cpp:242`, `Desktop::PhysicalToLogical` `Desktop.cpp:1572` |
| **Píxeles del framebuffer** | Píxeles físicos tras la rotación del panel: el buffer donde pinta `Painter` | DRM los llama "logical (post-rotation)": `DrmHWInterface.cpp:329`, `:1357`, `:1509`, `:3052`, `:3060` |
| **Píxeles de scanout** | Píxeles físicos del panel antes de rotar | `DrmHWInterface::_CopyToFront` `DrmHWInterface.cpp:3060-3090` |

Recomendación: fijar estos tres nombres en un `GLOSSARY.md` y en un ADR
(`docs/adr/`, todavía no existe) antes de tocar código. En DRM, "logical"
significa *rotado pero no escalado*, y esa colisión de nombres va a provocar
errores de conversión.

---

## 3. Mapa del estado actual

### 3.1 Definición del factor

| Paso | Referencia | Espacio/redondeo |
|---|---|---|
| Normalización `(0,4]` y fallback 1.0 | `ScreenScaleTransform.h:18-26` | float, sin cuantizar |
| Frame lógico de la pantalla `(right+1)/s - 1` | `ScreenScaleTransform.h:29-39` | **No entero** si `w/s` no es entero (1920×1080 @1.75 da 1097.14×617.14) [V] |
| Rect lógico → físico `floor(l*s)`, `ceil((r+1)*s)-1` | `ScreenScaleTransform.h:42-52` | Expansivo y conservador |
| Punto físico ↔ lógico (afín, float) | `ScreenScaleTransform.h:56-75` | Exacto en float; ida y vuelta probada en `src/tests/vos/test_screen_scale_transform.cpp:29-63` |
| Estado por pantalla `fScale` | `Screen.h:58-60,73`; `Screen.cpp:249-273` | `SetScale` propaga al `DrawingEngine` de la pantalla |
| Persistencia | `ScreenConfigurations.h:26-30,64-65`; `ScreenConfigurations.cpp:334,409` | float en settings |
| API de cliente | `BScreen::GetScale/SetScale` `Screen.cpp:374-387` (kit); `PrivateScreen.cpp:838-868`; `ServerApp.cpp:3488-3515` | Mensajes `AS_SCREEN_SET_SCALE`/`GET_SCALE` |
| Cambio en caliente | `Desktop::SetScreenScale` `Desktop.cpp:1511-1549` → `_ScreenChanged` `Desktop.cpp:4651-4705` | Recoloca ventanas en lógico y envía `B_SCREEN_CHANGED` con el frame lógico |

### 3.2 Propagación al render

| Paso | Referencia | Observación |
|---|---|---|
| Ventana → motor | `Window::SetScreen` `Window.cpp:677-690` | Cada `Window` tiene su `DrawingEngine`; un único factor por ventana (el de su `Screen`) |
| Motor → Painter | `DrawingEngine::SetScale` `DrawingEngine.cpp:298-310`; `Painter::SetScale` `Painter.cpp:372-381` | |
| Transformación de dispositivo | `Painter::_UpdateTransform` `Painter.cpp:452-473` | Escala alrededor del **origen de pantalla**; compuesta en orden incorrecto (F1) |
| Clipping | `Painter::_UpdateClipping` `Painter.cpp:414-437`, `ScaleRegion` `:399-411` | Cada rect de la región se expande por separado (F2) |
| Damage hacia el HW | `DrawingEngine::CopyToFront` `DrawingEngine.cpp:289-294`, `DrawTransaction` `:100-150` | Mismo `ScaleRegion` |
| Rutas "pre-escaladas" | `FillRect(color)` `:890`, `StrokeRect(color)` `:875`, `FillRegion(color)` `:905`, `InvertRect` `:640`, `StrokeLine(color)` `:841` | Escalan a mano y llaman a primitivas físicas o sin transformación |
| Rutas transformadas | `StrokeRect(r)` `:940`, `DrawBitmap` `:656`, `DrawString` `:1272` y otras | Usan `fTransform` |
| Blit de mover/scroll | `DrawingEngine::CopyRegion` `:541-636` (offset `roundf(off*s)` en `:550-551`); llamado desde `Desktop::MoveWindowBy` `Desktop.cpp:2186-2210` y `Window::CopyContents` `Window.cpp:464-526` | F3 |

### 3.3 Entrada y puntero

| Paso | Referencia | Espacio |
|---|---|---|
| Movimiento relativo + clamp | `LibEvdevEventStream::_FlushPendingEvents` `LibEvdevEventStream.cpp:652-667` | Físico (clamp a `fWidth/fHeight` físicos) |
| Límites del stream | `Desktop.cpp:4674` (`UpdateScreenBounds(physicalFrame)`), `Desktop.cpp:762` | Físico |
| Posición del cursor HW | `EventDispatcher.cpp:872-885` → `HWInterface::MoveCursorTo` `HWInterface.cpp:228-262` | Físico |
| Conversión para hit-test | `MouseFilter::Filter` `Desktop.cpp:315-327` | Físico → lógico (copia local; el mensaje sigue con `where` físico) |
| Segunda conversión | `DefaultWindowBehaviour::_RegionFor` `DefaultWindowBehaviour.cpp:1025-1038` | Lee el `where` físico del mensaje y lo vuelve a convertir |
| Hacia el cliente | `EventDispatcher.cpp:948-951` (borra `where`, añade `screen_where` lógico), `:597`, `_drop_point_` `:820` | Lógico |
| Estado consultable | `GetLastMouseState` (lógico, vía `MouseFilter`) para `AS_GET_CURSOR_POSITION` `ServerApp.cpp:1315-1330`; `EventDispatcher::GetMouse` `EventDispatcher.cpp:511-523` | `GetMouse` devuelve **físico**; no tiene llamadas en app_server (riesgo latente) |

### 3.4 Texto, bitmaps, decoradores y cursor

| Tema | Referencia | Estado |
|---|---|---|
| Texto | `AGGTextRenderer.cpp:144-148` (`NeedsVector` es true si la transformación no es solo traslación) → `FontCacheEntry::_RenderTypeFor` `FontCacheEntry.cpp:424-437` | Con `s≠1`, glifos en **contornos sin hinting ni subpíxel**, cacheados al tamaño lógico |
| Bitmaps | `BitmapPainter.cpp:87-170`, `_HasAffineTransform` `:251-254` | Con `s≠1` siempre `DrawBitmapGeneric`; no existe densidad de bitmap |
| Decorador | `TabDecorator::_DoLayout` `TabDecorator.cpp:237-265` | Escala por `font size / 12`, trunca `fBorderWidth` a `int32` y no conoce la escala de pantalla |
| Cursor | `Desktop.cpp:724,2483` (`InitializeCursors(boldFont/12)`); se pinta en el framebuffer físico (`DrmHWInterface.cpp:2518-2580`) | No se escala con la pantalla: a 1.5 se ve a 2/3 del tamaño lógico [V para el código; el efecto visual es S] |
| Kit cliente | `ControlLook.cpp:54-56`, `Slider.cpp:40`, `Alert.cpp:612`, `MenuBar.cpp:741` | Escala solo por tamaño de fuente; el cliente desconoce la escala de pantalla (correcto para BeAPI, ver O8) |

### 3.5 Geometría física expuesta a clientes

| API | Referencia | Problema |
|---|---|---|
| `BScreen::GetBitmap/ReadBitmap` | `PrivateScreen.cpp:341-390` (bounds = `Frame()` lógico) → `ServerApp.cpp:3564-3590` → `DrawingEngine::ReadBitmap` `DrawingEngine.cpp:1409-1440` | Lee el framebuffer **físico** con bounds **lógicos**: la captura sale recortada en la esquina superior izquierda [V] |
| `BDirectWindow` | `ServerWindow.cpp:4563-4566` → `DirectWindowInfo.cpp:179-190` | `window_bounds`/`clip_list` lógicos con `bits`/`bytes_per_row` del buffer físico [V] |

### 3.6 Puente Wayland

| Paso | Referencia | Estado |
|---|---|---|
| Salida wlr | `koan_server.c:262-268` | Modo fijo 1920×1080, `scale=1.0` |
| `koan_server_set_output_dimensions` | `koan_server.c:293-306`; `koan_server.h:52-55` | **Stub** (`TODO`) **sin ninguna llamada** en el árbol [V] |
| Protocolos | `wlroots-koan/CMakeLists.txt:87,102,330,369`; `servers/app/fractional-scale-v1-protocol.c`, `viewporter-protocol.c` | Se generan y compilan, pero en `server/` no hay `wlr_fractional_scale_manager_v1_create` ni `wlr_viewporter_create` [V] |
| Ventana nativa | `WaylandWindow.cpp:487` (`frame = buffer w×h`) | Toma los píxeles del buffer como coordenadas **lógicas** |
| Contenido | `WaylandWindow.cpp:665-720` (`SetViewBitmap` 1:1 en lógico) | `Painter` lo reescala por `s`: cliente borroso sin opción de enviar un buffer HiDPI |

### 3.7 Tests existentes

- `src/tests/vos/test_screen_scale_transform.cpp` (registrado en
  `src/tests/vos/CMakeLists.txt:23-27`): ida y vuelta de puntos, cobertura de
  un píxel y ancho del frame lógico para 1.0/1.25/1.5/1.75. **No prueba la
  adyacencia ni la partición de regiones, ni la coherencia entre render y
  hit-test.**
- `src/tests/servers/app/unit_tests/SimpleTransformTest.cpp`,
  `src/tests/servers/app/painter/`: no cubren la escala de pantalla [V por grep].

---

## 4. Hallazgos

### F1. Composición escala × transformación de vista invertida [V]

`Painter.cpp:467-472`:

```cpp
agg::trans_affine scaleTransform = agg::trans_affine_scaling(fScale);
scaleTransform *= fTransform;   // = "escalar, después aplicar U"
fTransform = scaleTransform;
```

En AGG, `a *= b` aplica primero `a` y después `b` (`agg_trans_affine.cpp:70-82`:
`tx = tx*m.sx + ty*m.shx + m.tx`). El resultado es `p → U(s·p)`, cuando lo
correcto es `p → s·U(p)`. La traslación del usuario (`U.tx`) y los offsets
`fTransformOffsetX/Y` (`Painter.cpp:455-462`) quedan **sin escalar**. Ejemplo:
vista con `SetTransform(scale 2)` en origen lógico `o=(100,0)` a `s=1.5`. Lo
correcto es `x_phys = 3p − 150`; el código da `3p − 100`, un desplazamiento de
50 px físicos. Afecta también a los gradientes (`_CalcLinearGradientTransform`
`:2275-2290` multiplica por `fTransform`). Corrección mínima:
`fTransform *= agg::trans_affine_scaling(fScale)`.

### F2. Las regiones se redondean rect a rect con expansión, sin conservar la partición [V]

- Dónde se redondea: `ScreenScaleTransform::ToPhysicalRect` (`floor`/`ceil`),
  aplicado a cada rect en `Painter::ScaleRegion` (`Painter.cpp:399-411`).
- Quién lo usa: el clipping (`_UpdateClipping` `:414-437`), el damage
  (`CopyToFront` `DrawingEngine.cpp:289-294`), `FillRegion` (`:905-935`) y
  `CopyRegion` (`:548`).
- Ejemplo a 1.25: el lógico `[0,9]` da `[0,12]` y el lógico `[10,19]` da
  `[12,24]`. **La columna física 12 pertenece a las dos regiones.** Ocurre en
  cualquier borde `e` con `e·s` no entero: a 1.5 en los bordes impares, a 1.75
  en 3 de cada 4.
- Consecuencias:
  - Una ventana pinta una columna que pertenece a la vecina o a la ventana que
    tiene encima. El color final depende del orden de repintado, lo que deja
    costuras y píxeles obsoletos al mover o redimensionar.
  - **Hit-test incoherente**: el píxel físico 12 equivale al lógico 9.6, que
    pertenece a la ventana izquierda, pero puede verse pintado por la derecha.
    `AGENTS.md` pide expresamente evitarlo ("inconsistent hit regions").
- Clipping y damage necesitan semánticas distintas. El clipping es una
  **partición**: cada píxel físico tiene un solo dueño, y basta con redondear
  cada *borde* con la misma función. El damage es una **cobertura**: se puede
  expandir, pero después debe intersecarse con el clipping físico.

### F3. Escala en espacio de pantalla: fase subpíxel dependiente de la posición; blits inexactos [V]

- `_UpdateTransform` escala alrededor de `(0,0)` de la pantalla. Una ventana
  en x lógica 1 empieza en 1.25 físico y en x=2 empieza en 2.5. El mismo
  contenido se rasteriza distinto según dónde esté la ventana (antialiasing
  distinto en los bordes; el texto "tiembla" al arrastrar) [V en el código, S
  en la percepción].
- `Window::MoveBy(int32,int32)` (`Window.cpp:279-332`) solo admite pasos
  lógicos enteros. `MoveWindowBy` hace blit con `CopyRegion(..., (int32)x,
  (int32)y)` (`Desktop.cpp:2205`), que pasa el offset a `roundf(x*s)`
  (`DrawingEngine.cpp:550-551`). Para `dx=1` a 1.25, el blit mueve 1 px físico
  cuando el repintado correcto movería 1.25. El contenido que se copia no
  coincide con el que se repintaría y, junto con F2, los bordes del blit
  difieren de los del clipping nuevo.
- Lo mismo ocurre con `Window::CopyContents` (`Window.cpp:464-526`), que usan
  `BView::CopyBits` y el scroll.

### F4. Con `s≠1` se pierde el alineado a píxel y los trazos son incoherentes [V]

- `fIdentityTransform = fUserTransform.IsIdentity() && fScale == 1.0f`
  (`Painter.cpp:454`) desactiva el `centerOffset` y los atajos de líneas rectas
  y rectángulos (`:605`, `:622`, `:855`, `:888`, `:1010`, `:1025`, `:1060`,
  `:1109`, `:1122`, `:1170`). Una línea lógica de 1 px a 1.25 se reparte con
  antialiasing entre dos columnas físicas.
- `DrawingEngine::StrokeLine(color)` usa `StrokeLineNoTransform` con
  `fPenSize = fScale` (`Painter.cpp:660-675`), es decir 1.25 px con AA.
  `StrokeRect(color)` pinta 1 px **físico** (`DrawingEngine.cpp:875-887`). El
  propio decorador mezcla, por tanto, grosores distintos.

### F5. El texto pierde hinting y subpíxel en cuanto `s≠1` [V]

`NeedsVector()` (`AGGTextRenderer.cpp:144-148`) devuelve true si la
transformación no es solo traslación, y eso fuerza `glyph_ren_outline`
(`FontCacheEntry.cpp:429-434`): sin hinting, sin AA subpíxel, con glifos
cacheados al tamaño lógico y escalados como vectores. Además, el bounding box
de glifo usado para clipping está marcado como incorrecto (`AGGTextRenderer.cpp:244-248`,
"TODO: not correct!").

### F6. Los bitmaps no tienen densidad [V]

Con `s≠1`, `BitmapPainter` toma siempre la ruta afín genérica
(`BitmapPainter.cpp:89,119,137,148,163` dependen de `!_HasAffineTransform()`).
Iconos, botones del decorador y los buffers Wayland se remuestrean desde el
tamaño lógico. No existe ninguna forma de aportar un bitmap a 1.5×.

### F7. Dos sistemas de escala independientes [V]

`font size / 12` en `TabDecorator.cpp:237` (con truncado
`int32(fBorderWidth * scaleFactor)` en `:263`, un redondeo intermedio en
lógico), cursores (`Desktop.cpp:724,2483`), `BControlLook::ComposeIconSize`
(`ControlLook.cpp:54-56`), `Slider.cpp:40` y `Alert.cpp:612`. Si el usuario sube
la fuente y además la escala de pantalla, los efectos se multiplican sin una
política definida. El cursor no participa de la escala de pantalla.

### F8. Entrada: conversión duplicada y espacios mezclados [V]

- `MouseFilter` convierte a lógico solo una copia local (`Desktop.cpp:321-327`),
  y `DefaultWindowBehaviour::_RegionFor` vuelve a convertir desde el mensaje
  (`DefaultWindowBehaviour.cpp:1031-1035`). Cualquier consumidor nuevo que lea
  `message->FindPoint("where")` recibirá coordenadas físicas.
  `NotifyMouseDown/Up/Moved` reciben a la vez el mensaje (físico) y `where`
  (lógico) (`Desktop.cpp:411,419,426`), así que los listeners (p. ej.
  `stackandtile/`) pueden tomar el campo equivocado [S: no he revisado cada
  listener].
- Los objetivos con *pointer grab* (`EventDispatcher.cpp:914-921`) saltan
  `MouseFilter`: `SetLastMouseState` no se actualiza [V].
- El movimiento relativo se acumula y se limita en píxeles físicos
  (`LibEvdevEventStream.cpp:655-666`). La velocidad del puntero en unidades
  lógicas cae como `1/s`, y esa política no está documentada [V para el código,
  S para la percepción].
- `PhysicalToLogical` elige la pantalla por contención física y
  `LogicalToPhysical` por contención lógica (`Desktop.cpp:1572-1624`). Solo son
  coherentes con una única pantalla (ver F9).

### F9. Diseño multipantalla y frame lógico no entero [V]

- `ScreenLogicalFrameAt` divide **también el origen** físico por la escala de
  la propia pantalla (`VirtualScreen.cpp:283-290`). Con pantallas de escala
  mixta, los frames lógicos se solapan o dejan huecos. Hoy el problema queda
  oculto porque `AddScreen` solo soporta una pantalla
  (`VirtualScreen.cpp:177`, TODO) y `_ScreenChanged` sustituye `fScreenRegion`
  por el frame de una sola pantalla (`Desktop.cpp:4673`).
- 1920×1080 @1.75 da un frame lógico de 1097.14×617.14. `BScreen::Frame()`
  devuelve un `BRect` fraccionario, cuando las ventanas solo se mueven en pasos
  enteros (`Window::MoveBy(int32…)`). Además, `ceilf` sobre productos float sin
  épsilon puede rebasar el borde físico en un píxel (por ejemplo, si
  `1097.142857f*1.75f` da `1920.0001f`) [S: depende de la representación
  float; no lo he medido].

### F10. Fugas de geometría física en la BeAPI [V]

`ReadBitmap` y `BDirectWindow` (tabla 3.5). La API de *direct window* es física
por naturaleza (acceso directo al framebuffer), pero hoy recibe bounds y clip
lógicos, de modo que un cliente que la use escribe en píxeles equivocados con
`s≠1`.

### F11. El puente Wayland no participa de la escala [V]

Ver la tabla 3.6. Para el protocolo, un buffer de cliente equivale hoy a
`buffer_scale=1` sobre una salida `scale=1`, mientras que app_server lo amplía
por la escala real de la pantalla. No hay ruta por la que el cliente sepa que
debe renderizar a 1.5×.

### F12. Menores [V]

- `SimpleTransform::_Apply(int32&, int32&)` trunca la escala:
  `x *= (int32)fScale` (`SimpleTransform.h:221-227`). Es la escala de vista de
  `BView::SetScale` (heredada de Haiku), pero bloquea cualquier intento de
  meter la escala de pantalla en `SimpleTransform`.
- `Normalize` no cuantiza, así que valores como 1.3333… producen bordes que
  nunca coinciden con la rejilla física.

---

## 5. Oportunidades priorizadas

Cada oportunidad indica: **seam** propuesto, **tests deterministas** que habilita,
**riesgo BeAPI** y **orden**. Prioridad = (corrección × alcance) / riesgo.

### O1. `OutputTransform`: módulo profundo de geometría de salida (prioridad 1, base de todo)

- **Problema que resuelve:** F2, F9 (redondeo y épsilon), F12 (cuantización),
  y deja un único sitio donde viven el factor y el redondeo. Hoy la lógica está
  repartida entre `ScreenScaleTransform`, `Painter::Scale*`,
  `DrawingEngine::CopyRegion` y `Desktop::PhysicalToLogical`.
- **Seam/interfaz** (valor inmutable en `src/servers/app/`, sin dependencias;
  amplía `ScreenScaleTransform.h`):
  - Escala racional `num/120` (el mismo denominador que `wp_fractional_scale_v1`:
    1.25=150, 1.5=180 y 1.75=210, exactos) con origen lógico y origen físico.
  - `int32 SnapEdge(float logicalEdge)` → borde físico (*half-open*,
    `round(e·num/120)` en aritmética entera). Es la única función de redondeo.
  - `BRegion ToPhysicalPartition(const BRegion&)`: ajusta los bordes con
    `SnapEdge`, lo que garantiza que los rects adyacentes no se solapen ni dejen
    huecos. Para clipping y visible regions.
  - `BRegion ToPhysicalCover(const BRegion&)`: expansiva, solo para damage.
  - `BPoint ToLogical(BPoint phys)` / `BPoint ToPhysical(BPoint log)` y
    `BPoint PixelCenterToLogical(int32 x, int32 y)` para hit-test.
  - `BRect LogicalFrame()` entero según una política documentada (p. ej.
    `floor(phys/s)`, con el resto físico como margen sin uso o asignado a la
    última columna).
- **Tests deterministas habilitados** (host, como `test_screen_scale_transform`):
  - Para s ∈ {1, 1.25, 1.5, 1.75} y rejillas aleatorias de rects lógicos
    adyacentes: `ToPhysicalPartition` no solapa y no deja huecos.
  - **Coherencia render/hit-test:** para cada píxel físico, el dueño según la
    partición coincide con `Contains(PixelCenterToLogical(px))` en lógico.
  - Ida y vuelta de puntos con error < 1e-4, y `LogicalFrame()` con bordes
    enteros sin rebasar el frame físico.
  - `Cover ⊇ Partition` para cualquier región.
- **Riesgo BeAPI:** bajo (interno). `BScreen::Frame()` pasaría a ser entero en
  1.75, lo que en la práctica es más compatible que hoy.
- **Orden:** 1.

### O2. Corregir el orden de composición en `Painter::_UpdateTransform` (prioridad 2)

- **Problema que resuelve:** F1.
- **Seam:** `Painter` recibe la transformación de dispositivo (de O1/O3) y la
  compone **después** de la del usuario. Es un cambio de una línea, aislado.
- **Tests:** test de `Painter` sobre buffer en memoria (`BitmapHWInterface` /
  `src/tests/servers/app/painter/`). Un `FillRect` con
  `SetTransform(translate(10,0))` a s=1.5 debe empezar en x física 15. Con
  `scale(2)` sobre un origen de vista ≠ 0 se compara contra `s·U(p)` y se
  repite con gradientes.
- **Riesgo BeAPI:** bajo; corrige `BView::SetTransform` y no cambia la API.
- **Orden:** 2. Es independiente de O1 y podría ir primero como parche
  aislado.

### O3. Transformación de dispositivo por ventana con origen ajustado a píxel (prioridad 3)

- **Problema que resuelve:** F3, y el blit correcto al mover.
- **Seam:** `Window` le da a su `DrawingEngine` una `OutputTransform` con
  `physicalOrigin = SnapEdge(frame.left/top)` en lugar del factor de pantalla
  (hoy `Window::SetScreen` `Window.cpp:677-690`). El contenido se rasteriza
  como `translate(physOrigin)·scale(s)·translate(-logOrigin)`, invariante a la
  posición. `CopyRegion` recibe el delta **físico entero**
  `ΔphysOrigin` en lugar de `roundf(dx*s)`. Para `CopyBits`/scroll con un `dy·s`
  no entero, la regla es: repintar en lugar de hacer blit, o ajustar el scroll
  a múltiplos que den píxeles enteros.
- **Tests:**
  - (a) Render de una misma vista con la ventana en x=1 y en x=2: los bitmaps
    físicos deben ser idénticos salvo la traslación entera.
  - (b) "Blit = repintado": mover una ventana con un blit y comparar
    píxel a píxel con un repintado completo, en las cuatro escalas.
  - (c) Damage tras mover o redimensionar ventanas solapadas: ningún píxel
    obsoleto. Basta comparar el framebuffer con un repintado completo.
- **Riesgo BeAPI:** medio. `BView::CopyBits` y `ScrollBy` pueden dejar de
  hacer blit en algunos pasos (cambia el rendimiento, no la semántica). El
  frame lógico de la ventana no cambia.
- **Orden:** 4, junto con O4.

### O4. Pipeline de regiones con dos semánticas y una sola ruta en `DrawingEngine` (prioridad 3)

- **Problema que resuelve:** F2 aplicado. `DrawingEngine` tiene hoy dos
  familias de métodos ("pre-escalados" y "transformados", §3.2) que redondean
  cada una a su manera.
- **Seam:** `Painter::ConstrainClipping` recibe la partición física
  (`ToPhysicalPartition`); `DrawTransaction`/`CopyToFront` usan
  `ToPhysicalCover ∩ clipping`. Las primitivas internas del servidor
  (`FillRect(color)`, `FillRegion`, `StrokeRect(color)`, `InvertRect`) se
  expresan como "rect lógico → `SnapEdge` → fill físico", en un único helper
  privado de `DrawingEngine`, y se elimina el pre-escalado ad hoc.
- **Tests:** dos ventanas adyacentes y solapadas: ningún píxel físico está en
  el clipping de ambas; el `FillRegion` del fondo no pinta sobre ventanas; el
  damage tras `Invalidate` cubre todos los píxeles que cambian (comparando con
  un repintado completo).
- **Riesgo BeAPI:** bajo.
- **Orden:** 4 (necesita O1).

### O5. Una sola conversión de entrada, en la frontera stream → dispatcher (prioridad 2)

- **Problema que resuelve:** F8.
- **Seam:** un `InputTransform` (adaptador fino sobre O1) en
  `EventDispatcher::_EventLoop`, justo al leer el evento (`EventDispatcher.cpp:868-873`).
  - `where` pasa a lógico para todo el servidor; el físico se guarda solo para
    el cursor HW (`fLastCursorPosition`).
  - Se elimina la reconversión de `DefaultWindowBehaviour.cpp:1035`.
  - `GetMouse` devuelve lógico.
  - Política explícita de velocidad: el delta relativo se acumula en lógico
    (`Δ/s`) o en físico, documentado. El clamp se hace al frame lógico virtual.
- **Tests:** stream sintético (ya existe infraestructura de stream/adapter:
  `TestServerLoopAdapter.*`, `src/tests/vos/testinput.cpp`). Se inyecta un clic
  en cada píxel físico de un borde entre ventanas y se comprueba que la ventana
  que recibe el clic es la dueña del píxel según O1. También el umbral de
  doble clic (`square_distance >= 16`, `Desktop.cpp:375`) y los umbrales de
  arrastre en lógico, y el `_drop_point_` lógico.
- **Riesgo BeAPI:** bajo-medio. Los clientes nunca reciben `where` del servidor
  (se borra en `EventDispatcher.cpp:948-951`), pero los *input filters* y
  listeners internos deben auditarse.
- **Orden:** 3.

### O6. Métricas de decorador y UI como tokens lógicos con una política de escala única (prioridad 4)

- **Problema que resuelve:** F7, y es prerrequisito del decorador moderno de
  `AGENTS.md`.
- **Seam:** un `DecoratorMetrics` o tokens de tema calculados una sola vez a
  partir de `uiScale = fontScale` (en lógico, sin truncar a `int32`). La escala
  de pantalla **no** entra en las métricas: la aplica solo el dispositivo (O3).
  `TabDecorator::_DoLayout` consume esos tokens. El cursor se rasteriza a
  `fontScale × screenScale` en físico.
- **Tests:** para s ∈ {1…1.75} × fontScale ∈ {1, 1.25}: los rects de título,
  botones y tab no se solapan; `RegionAt` sobre `PixelCenterToLogical` de cada
  píxel físico del botón dibujado devuelve ese botón (coherencia entre hit-test
  y render en el decorador); hay anchos mínimos y overflow.
- **Riesgo BeAPI:** medio. El comportamiento heredado de Haiku ("subir la
  fuente agranda la UI") debe mantenerse; solo se separa de la escala de
  pantalla.
- **Orden:** 5.

### O7. Alineado a píxel en espacio físico (prioridad 5)

- **Problema que resuelve:** F4.
- **Seam:** dentro de `Painter`, distinguir "transformación = escala +
  traslación" de "transformación general". En el primer caso, aplicar el
  alineado **después** de transformar (al rasterizar, como pide `AGENTS.md`) y
  reactivar los atajos. Para los trazos de UI (`StrokeLine(color)`,
  `StrokeRect(color)`), usar `physicalWidth = max(1, round(w·s))` de forma
  coherente.
- **Tests:** a cada escala, una línea lógica horizontal de 1 px produce filas
  físicas totalmente opacas (sin franjas de AA), y `StrokeRect(color)` y
  `StrokeLine(color)` producen el mismo grosor.
- **Riesgo BeAPI:** bajo-medio (cambio visual en apps que dependan del AA
  actual; solo con `s≠1`).
- **Orden:** 6.

### O8. Texto rasterizado a tamaño físico (prioridad 5)

- **Problema que resuelve:** F5.
- **Seam:** `FontCacheEntry::_RenderTypeFor` y `AGGTextRenderer` aceptan una
  "escala de dispositivo" separada de la transformación de usuario. Los glifos
  se rasterizan a `size·s` con hinting/subpíxel y la posición del pen es
  `s·p`. Las **métricas de layout siguen siendo lógicas** y provienen de
  avances lineales (sin hinting), para que `StringWidth` no dependa de la
  escala.
- **Tests:** `StringWidth`/`GetEscapements` idénticos en las cuatro escalas;
  los bounds físicos de cada glifo están dentro de `SnapRect(boundsLógicos)` con
  ±1 px; la clave de la caché incluye la escala de dispositivo.
- **Riesgo BeAPI:** medio. Si se usaran avances con hinting a tamaño físico, el
  ancho del texto cambiaría con la escala y rompería layouts. El diseño anterior
  lo evita.
- **Orden:** 7.

### O9. Densidad de bitmaps y escalado filtrado (prioridad 6)

- **Problema que resuelve:** F6.
- **Seam:** `ServerBitmap` con un `density` opcional (por defecto 1). Para
  `scale+translate` puro, `BitmapPainter` pasa por una ruta de reescalado
  filtrado (bilinear) al rect `SnapRect` destino. Los iconos vectoriales (HVIF)
  se re-renderizan a tamaño físico en el lado cliente vía `ComposeIconSize`
  (aditivo).
- **Tests:** `DrawBitmap` de 16×16 a 1.5 cubre exactamente `SnapRect(dest)` (24×24),
  sin sangrado fuera del rect; con `density=1.5` y 24×24 el blit es 1:1.
- **Riesgo BeAPI:** bajo si es aditivo (nuevo flag o constructor; `BBitmap`
  existente sin cambios).
- **Orden:** 8.

### O10. Diseño multipantalla en espacio lógico (prioridad 7)

- **Problema que resuelve:** F9.
- **Seam:** `VirtualScreen` guarda la **posición lógica** de cada pantalla
  (configurable) y deriva la física por pantalla. `PhysicalToLogical` recorre
  las pantallas por su `OutputTransform`. `fScreenRegion` pasa a ser la unión
  de todas.
- **Tests:** con dos pantallas de escala mixta (1.0 + 1.5), los frames lógicos
  son contiguos y sin solape; al cruzar el borde, el puntero no salta ni se
  atasca; una ventana movida entre pantallas mantiene su tamaño lógico (política
  de `AGENTS.md`).
- **Riesgo BeAPI:** bajo (`BScreen` ya es lógico).
- **Orden:** 9. Depende de que exista soporte real multipantalla
  (`VirtualScreen.cpp:177`).

### O11. Contratos explícitos para las APIs físicas de la BeAPI (prioridad 6)

- **Problema que resuelve:** F10.
- **Seam:**
  - `BScreen::GetBitmap/ReadBitmap`: el servidor convierte los bounds lógicos
    con `ToPhysicalCover` y devuelve un bitmap de **tamaño lógico**
    (reescalado). Opcionalmente, una variante física aditiva.
  - `BDirectWindow`: `DirectWindowInfo::SetState` recibe la geometría física
    (`ToPhysicalPartition` del frame y del clip), documentando que
    `direct_buffer_info` está en píxeles físicos.
- **Tests:** las dimensiones de `GetBitmap()` coinciden con `Frame()` en las
  cuatro escalas; `window_bounds`/`clip_list` coinciden con la partición física
  de la ventana.
- **Riesgo BeAPI:** **alto** para `BDirectWindow` (apps que asumen que las
  coordenadas de ventana coinciden con las del framebuffer). Con `s=1` no hay
  cambio. Para `s≠1`, lo prudente es documentarlo y quizá negar el modo directo.
- **Orden:** 10.

### O12. Escala en el puente Wayland (prioridad 7; coordinar con la sesión de wlroots-koan)

- **Problema que resuelve:** F11.
- **Seam:**
  - (a) `Desktop` (o el `WaylandWindowBridge`) llama a
    `koan_server_set_output_dimensions(physW, physH, scale)` al arrancar y en
    `_ScreenChanged`, y el stub aplica modo y escala al `wlr_output`.
  - (b) Se crean `wlr_fractional_scale_manager_v1` y `wlr_viewporter`, y se
    envía `preferred_scale = num` (el mismo racional `/120` de O1).
  - (c) Una función pura `(buffer w×h, buffer_scale, viewport dst) → tamaño
    lógico`. El frame nativo usa el tamaño lógico, y `SetViewBitmap` con
    destino `SnapRect` 1:1 en físico, sin remuestreo, cuando el cliente
    renderiza a la escala preferida.
- **Tests:** tests unitarios de la función pura (C, en host); test de cola
  comprobando que un cambio de escala en app_server emite un comando con
  `num/120`. **No tocar** el directorio `src/add-ons/wlroots-koan/tests/` sin
  commitear de la otra sesión: proponer los tests allí cuando lo haya
  integrado.
- **Riesgo BeAPI:** ninguno.
- **Orden:** 11 (necesita O1 y O3).

### O13. Documentación (transversal)

Crear `GLOSSARY.md` con los tres espacios de §2 y un ADR "Coordenadas lógicas,
framebuffer y scanout; política de redondeo de bordes" en `docs/adr/`. Este
paso no tiene riesgo y evita que futuras conversiones se dupliquen (F8) o usen
"logical" con el sentido de DRM.

---

## 6. Orden de implementación recomendado

| # | Paso | Depende de | Motivo del orden |
|---|---|---|---|
| 0 | O13 (glosario + ADR) | — | Fija el vocabulario antes de tocar código |
| 1 | O1 `OutputTransform` + tests host | — | Seam base; es puro y se prueba en host |
| 2 | O2 composición en `Painter` | — | Bug aislado de una línea |
| 3 | O5 conversión única de entrada | O1 | Habilita los tests de hit-test en bordes |
| 4 | O4 + O3 regiones y origen por ventana | O1 | Resuelven juntos costuras, píxeles obsoletos y blits |
| 5 | O6 métricas de decorador | O1, O3 | Prerrequisito del decorador moderno y de las tabs |
| 6 | O7 alineado físico | O3 | Necesita la fase estable que aporta O3 |
| 7 | O8 texto físico | O3 | Ídem |
| 8 | O9 bitmaps y cursor | O1 | |
| 9 | O10 multipantalla | O1, soporte multi-salida | |
| 10 | O11 APIs físicas | O1, O4 | Mayor riesgo BeAPI; al final |
| 11 | O12 Wayland | O1, O3 | Coordinar con la sesión de wlroots-koan |

Este orden coincide con el paso 5 de "Implementation Order" de `AGENTS.md`
("Correct fractional scaling…") y adelanta O6 respecto al decorador moderno
(paso 3). **Discrepancia a decidir:** `AGENTS.md` pone el decorador antes que
el escalado, pero los hit-tests del decorador (traffic lights, tabs) solo serán
coherentes a 1.25/1.75 después de O1, O4 y O5. Recomiendo O1, O2 y O5 antes de
los controles nuevos del decorador.

---

## 7. Validación

- Builds y tests: **NOT RUN** (el encargo era solo análisis; hay un rebuild
  Release en curso en `generated.release/` que no se ha tocado).
- No se ha modificado código de producción. Solo se ha añadido este documento.
- Las afirmaciones [V] se basan en lectura de código; los ejemplos numéricos
  son aritmética sobre las fórmulas citadas, no mediciones.

## 8. Suposiciones y puntos no verificados

- [S] Efecto visual de F3 (temblor del texto al arrastrar) y del tamaño del
  cursor: deducido del código, no observado en QEMU.
- [S] Que los `DesktopListener` (`stackandtile/`) lean el `where` físico del
  mensaje: no he revisado cada listener.
- [S] El desbordamiento por `ceilf` sin épsilon en el frame lógico a 1.75
  depende del redondeo float concreto.
- [S] Que `BScreen::GetMode()` (físico) y `BScreen::Frame()` (lógico) se mezclen
  en clientes: no he auditado las apps.
- No trazado: el reenvío del puntero a superficies Wayland (`koan_adapter.c`) y
  `RemoteHWInterface`/`AccelerantHWInterface` con escala.
