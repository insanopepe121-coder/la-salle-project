# Launcher

Launcher real para un juego open-source hecho en Godot. Revisa actualizaciones,
las descarga y aplica, gestiona mods y lanza el juego.

## Estructura esperada junto al .exe del launcher

```
Launcher.exe
version.txt          <- se crea solo, guarda la versión instalada
launcher_config.ini  <- se crea solo, guarda la URL del manifiesto
game/
  Game.exe           <- lo que exportes desde Godot
  mods/              <- aquí la gente mete sus mods
```

## Cómo compilar

En la consola **"x64 Native Tools Command Prompt for VS"**, dentro de esta carpeta:

```
rc app.rc
cl /EHsc /std:c++17 /DUNICODE /D_UNICODE /O2 main.cpp app.res /Fe:Launcher.exe ^
   /link user32.lib gdi32.lib comctl32.lib uxtheme.lib msimg32.lib winhttp.lib shell32.lib
```

Necesitas un `icon.ico` en esta misma carpeta (puedes usar el que ya tenías).

## Cómo funciona la actualización

1. Subes a tu repositorio de GitHub un archivo `version.json` (puedes usar el de ejemplo
   que te dejé) con la versión actual y la URL de un .zip con el juego exportado.
2. En GitHub, ve a **Releases → Draft a new release**, sube el .zip con el juego
   (todo lo que exporte Godot) y copia el link de descarga directo del asset.
3. Pon ese link en el campo `"url"` de `version.json`.
4. Súbelo a tu repo como `main/version.json` y copia la URL en formato "raw"
   (botón "Raw" en GitHub). Esa es la URL que va en `MANIFEST_URL` dentro del
   launcher, o la que pegas en la pestaña Configuración.

Cada vez que saques una versión nueva: subes el nuevo .zip como Release, subes
un `version.json` con el número de versión actualizado, y quien abra el
launcher verá el aviso de actualización automáticamente.

## Mods

El launcher solo activa/desactiva archivos en `game/mods` (renombrando a
`.disabled`). La lógica de **cargar** esos mods dentro del juego depende de
cómo diseñemos el sistema de mods en Godot (siguiente paso).

## Estructura de repo sugerida para todo el proyecto

```
mi-juego/
├── launcher/        <- esta carpeta
├── game/             <- proyecto de Godot (el código fuente del juego)
│   ├── core/         <- GDExtension en C++ (sistemas pesados)
│   ├── scripts/       <- GDScript (lógica, UI)
│   └── mods/          <- plantilla vacía, se llena en tiempo de ejecución
├── docs/
│   └── MODDING.md     <- guía para quien quiera hacer mods
└── README.md
```
