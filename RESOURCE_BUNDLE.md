# Bundle local de recursos

`resources-tx` sigue siendo el dump maestro. El juego no distribuye ni vuelve a leer sus ~10.000 archivos completos.

## Crear el autoextraible

Con `resources-tx` clonado al lado de este repositorio:

```bat
build-resource-pack.cmd
```

O indicando otra ubicacion:

```bat
build-resource-pack.cmd "D:\ruta\resources-tx"
```

El resultado es:

```text
build\Release\PromptemonResources.exe
```

El empaquetador inspecciona el dump y selecciona automaticamente lo que usa el slice actual:

- los 8 Pokemon definidos por el juego y sus BCH de animacion compartida;
- una escena BCH real y utilizable para el Hub, priorizando `stage/town1`;
- la familia de archivos de esa escena;
- una fuente BCFNT que realmente decodifica;
- los recursos que realmente decodifican como panel/boton de UI;
- un efecto BCH utilizable para el feedback de combate, si existe.

Los archivos LZH8/LZ11 seleccionados se guardan ya descomprimidos dentro del bundle. La ROM cruda, los otros Pokemon, idiomas/UI no elegidos, escenarios ajenos y el resto del dump no entran en el EXE.

Al ejecutar `PromptemonResources.exe`, se instala el subset junto al ejecutable en:

```text
assets\resources-tx\
```

`promptemon_demo.exe` consume directamente ese bundle instalado. En el flujo normal ya no pide seleccionar una ROM ni vuelve a extraer el dump.

El autoextraible se genera localmente; ni el dump ni sus recursos se agregan al repositorio de codigo.
