# promptemon-steidium

Prototipo de combate de criaturas por turnos en 3D, inspirado en la estructura de un juego tipo Stadium.

## Prototipo jugable actual

`promptemon_demo` ya implementa un circuito jugable mínimo:

- Menú inicial con Nueva partida y opciones básicas.
- Introducción corta.
- Elección inicial entre Rattata, Pidgey y Caterpie.
- Proto Hub.
- Zona de Caza con encuentros ponderados: los Pokémon comunes aparecen con mucha más frecuencia.
- Cada encuentro muestra qué estadística aporta al entrenamiento de IV.
- Los encuentros pueden ser shiny; el corte actual usa una tirada de 1/4096.
- Combate local por turnos con PS, velocidad, daño, victoria y derrota.
- Ganar otorga experiencia y puede mejorar el IV indicado por el rival, hasta 31.
- Cargar, borrar y exportar partida permanecen deshabilitados en este corte.

La captura, persistencia local, colección completa y multijugador se conectarán sobre este circuito una vez validado el combate.

## Objetivo general

- Combate 1 vs 1 por turnos.
- Conexión multijugador reutilizando y adaptando la capa de transporte UDP publicada con Command & Conquer Generals / Zero Hour.
- Las decisiones de cada turno se sincronizan por red; las animaciones y presentación se ejecutan localmente.
- Carga de datos y recursos desde archivos aportados por el usuario separada del subsistema de red.
- Los Pokémon encontrados pertenecen a la base local del jugador y podrán capturarse, entrenarse y usarse en combate.

## ROM y visor 3D

El trabajo anterior de lectura de ROM, BCH, modelos, texturas y animaciones no se eliminó. Se conserva como el target `promptemon_viewer`, mientras `promptemon_demo` se usa para iterar rápido sobre la jugabilidad.

## Código de C&C

La referencia original procede de `electronicarts/CnC_Generals_Zero_Hour`. El código de Electronic Arts copiado en `third_party/cnc/` conserva sus cabeceras de copyright y licencia. No se importan los sistemas RTS, GameSpy, GUI, descargas ni la sincronización por frames que no son necesarios para este proyecto.
