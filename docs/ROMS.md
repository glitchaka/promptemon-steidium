# ROMs locales

La carpeta `roms/` es la biblioteca local persistente de ROMs de `promptemon-steidium`.

Puedes colocar varias ROM a la vez. El cargador recorre la carpeta y subcarpetas, identifica GB/GBC/GBA/NDS/CIA/3DS/CCI/CXI y combina los recursos encontrados en fuentes compatibles.

Las ROM de GB/GBC/GBA/NDS se reconocen como fuentes de datos, pero no se inventan modelos 3D que esas ROM no contienen. Las fuentes 3DS con RomFS legible y recursos BCH pueden aportar modelos, texturas y animaciones al juego.

## Importante

`roms/` está completamente excluida de Git y del proceso de limpieza de compilación. Un `git pull` normal y `build.cmd` no deben borrar, reemplazar ni recrear su contenido. La compilación sólo limpia `build/`.

No subas ROMs comerciales al repositorio público. Déjalas sólo en tu copia local dentro de `roms/`.
