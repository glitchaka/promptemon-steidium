# promptemon-steidium

Prototipo de combate de criaturas por turnos en 3D, inspirado en la estructura de un juego tipo Stadium.

## Objetivo inicial

- Combate 1 vs 1 por turnos.
- Conexión multijugador reutilizando y adaptando la capa de transporte UDP publicada con Command & Conquer Generals / Zero Hour.
- Las decisiones de cada turno se sincronizan por red; las animaciones y presentación se ejecutan localmente.
- Carga de datos y recursos desde archivos aportados por el usuario se mantendrá separada del subsistema de red.

## Código de C&C

La referencia original procede de `electronicarts/CnC_Generals_Zero_Hour`. El código de Electronic Arts copiado en `third_party/cnc-network/` conserva sus cabeceras de copyright y licencia. No se importan los sistemas RTS, GameSpy, GUI, descargas ni la sincronización por frames que no son necesarios para este proyecto.
