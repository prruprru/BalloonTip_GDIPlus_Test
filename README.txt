# BalloonTip GDI+ Test para VFP9

Esta es una prueba de concepto independiente. No modifica `ctl32_BalloonTip` ni requiere integrar todavía el código en `ctl32.vcx`.

## Qué dibuja

- Ventana Win32 `WS_EX_LAYERED`, sin activar el foco.
- Fondo blanco, borde oscuro y esquinas redondeadas.
- Pico triangular.
- Sombra translúcida aproximada por capas.
- Icono de advertencia dibujado.
- Texto y título con fuentes GDI+ independientes.

## Compilación

El proyecto está preparado para Visual Studio 2022, Release | Win32, toolset v143.
También incluye `appveyor.yml` para compilarlo en AppVeyor.
El resultado se genera en `bin\\BalloonTipTest.dll`.

## Prueba en VFP9

1. Compilar la DLL.
2. Copiar `bin\\BalloonTipTest.dll` junto a `PruebaBalloonTip.prg`.
3. Ejecutar el PRG desde VFP9 de 32 bits.
4. El aviso aparecerá al activarse el formulario y desaparecerá después de 8 segundos.

## Convención de coordenadas

`BT_Show(hOwner, x, y, ...)` interpreta x e y como coordenadas relativas al área cliente de la ventana propietaria. Si hOwner no es una ventana válida, se interpretan como coordenadas de pantalla.

## Exportaciones

- `BT_Show`: muestra o actualiza el aviso.
- `BT_Hide`: lo oculta.
- `BT_Destroy`: destruye la ventana y libera GDI+.
- `BT_Version`: devuelve 100 (versión 1.00).

## Alcance de esta prueba

Es una primera prueba gráfica, no una sustitución terminada del Tooltip original. La sombra está simulada mediante capas translúcidas; no es todavía un desenfoque gaussiano. La geometría, tamaños y posiciones son valores iniciales para comprobar el enfoque visual y la interoperabilidad con VFP9.
