# Bundle local de recursos

`resources-tx` es el dump completo. Promptemon no necesita copiarlo entero para el slice jugable actual.

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

El EXE contiene unicamente los archivos que coinciden con `config/required_resources.txt`. Tambien elimina del payload la copia comprimida cuando ya fue seleccionada su version decodificada de `files/decoded`.

Al ejecutar `PromptemonResources.exe`, el bundle se extrae junto al ejecutable en:

```text
assets\resources-tx\
```

El autoextraible se genera localmente: ni el dump ni sus recursos se agregan al repositorio de codigo.

## Mantener el subset

`config/required_resources.txt` es la fuente de verdad. Cuando el juego empiece a usar otra especie, escenario, UI o familia de efectos, se agrega una regla y se vuelve a generar el autoextraible. Las reglas disponibles son `prefix=`, `contains=`, `suffix=` y `basename=`; todas son case-insensitive.
