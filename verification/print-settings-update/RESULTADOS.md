# Ajustes de impresión 0.6.3 — resultados

2026-10-27, America/Bogota. **0.6.3-print-settings** cargada y verificada en
COM8, ESP32-P4 v1.3, USB `80:F1:B2:D1:7F:0B`.

## Entregado

- **IMPRIMIR** de G-codes abre inmediatamente la pantalla de estado al aceptar
  la solicitud. Muestra inicio pendiente hasta recibir el estado real de Klipper;
  no confunde la respuesta HTTP con una impresión iniciada ni duplica el envío.
- **AJUSTES IMPRESION** sustituye a Toolhead en impresión y pausa, conservando
  seis botones iguales y el icono Ajustes aprobado.
- Velocidad 10–200 %, flujo 50–150 % y ventilador de capa 0–100 %, con valores
  **ACTUAL/PROPUESTO**, paso de 5 puntos porcentuales y **APLICAR por fila**.
- Abrir, preparar un valor o cerrar no envía ajustes. Los datos ausentes se
  indican y deshabilitan. Cambiar de trabajo, estado, destino o conexión invalida
  el panel y las órdenes pendientes correspondientes.
- Movimiento XY/Z, homing, motores OFF y acceso a Toolhead bloqueados durante
  impresión, pausa o inicio pendiente. Extruir/retraer de pausa se conservan.
- Iconos y logos de la versión 0.6.2 sin cambios; el componente gráfico conserva
  exactamente su hash. Los controles nuevos usan los iconos aprobados.

Contrato y fuentes oficiales: `../../docs/ajustes-impresion-0.6.3.md`.

## Verificación

**156/156 escenarios**, **533 eventos LVGL**, once vistas de 800 × 480 y
cero etiquetas recortadas. Se conservan los 115 escenarios anteriores y se
añaden 41 de navegación, doble toque, respuestas rechazadas/inciertas,
controles de ajuste, límites, valores ausentes/no finitos y cambios de contexto.
Modal, pantallas principales, datos ausentes y estado pendiente revisados
visualmente mediante capturas del renderer LVGL productivo.

Las **seis regresiones C++** pasan con el compilador ESP32 y flags estrictos.
La compilación ESP-IDF 5.5.6 pasa; el binario es posterior a las últimas fuentes
productivas. Las huellas de las fuentes probadas coinciden con las archivadas.

**38/38 controles en placa**: versión cargada, arranque, once vistas, navegación
de inspección, vuelta al modo normal, ajustes, conexión y ausencia de fallos.
**7/7 comprobaciones de lectura con Moonraker**: preferencias, macros públicas,
conexión, valores reales de ajustes, PNG 300, filamentos y ausencia de fallos.
Se recibieron velocidad 100 %, flujo 100 % y ventilador 0 %, coincidentes con GET.

La prueba de metadatos leyó un G-code de un solo filamento activo entre ocho
posiciones configuradas. Después se observó un trabajo activo distinto con cinco
filamentos usados. Se refrescó la miniatura del trabajo activo de 300 × 300 y
se dejó el panel en su vista normal, sin inspección y con COM8 cerrado.

La carga se hizo en espera, con objetivos térmicos a cero. Durante la verificación
posterior empezó una impresión: quedó en marcha. Estas pruebas no enviaron
órdenes de impresión, pausa, movimiento, calentamiento, ajustes o reinicio de
Klipper. Los envíos de los nuevos controles se prueban con servicios simulados;
su uso físico durante un trabajo queda a la observación del usuario.

## Artefactos y ajustes

Aplicación: **3074272 bytes**, partición de 4 MiB, **27 % libre**.
SHA-256 instalado y verificado por esptool:

```text
6f275eeb912e6d19ee18563b22245b25336fdad20fb79d85f7ab594af06eef2e
```

Se escribió solo la aplicación en `0x10000`. NVS se conservó: Moonraker
`192.168.0.235:7125`, brillo 59 %, reposo 1 min, sonido activado y volumen 82 %,
según la lectura inmediatamente anterior a la carga. El respaldo de 4 MiB
`application-before.bin` coincide con la aplicación 0.6.2 instalada anteriormente.

Evidencias: `firmware-manifest.json`, `firmware-0.6.3/`, `build.log`,
`backend-regressions.log`, `flash-application.log`, `device-inspection.json`,
`device-metadata.json`, `final-live-status.json`, `host-action-report.json`,
`qa_tuning_*.png` y `qa_print*.png`.
