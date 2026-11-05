# Revisión de flujo 0.6.1 — resultados

2026-10-26, America/Bogota. `0.6.1-ui-flow` cargada en COM8,
ESP32-P4 v1.3, USB80:F1:B2:D1:7F:0B. Aplicación2.859.264bytes,
partición4MiB con32% libre. SHA256 instalado y verificado por esptool:
`9ac5febc06672565fa3fa257e8bc4497d287383e1352ce580a6b5d6667873fba`.

## Entregado

- Lista de nombres sin imágenes, filas80px y selección sin reconstrucción.
- Una vista previa300×300 y círculos de colores activos34px; sin tiempos,
  cantidades, nombres de filamento ni identificadoresT0/T1/etc en G-codes.
- Eliminación completa de precarga/caché de miniaturas de lista. Selección
  prioritaria, cancelación de transferencias obsoletas y copias PNG fuera del lock.
- Teclado numérico con punto para IP de Moonraker.
- GLaDOS original en espera/completado/cancelado y fallo.

Los nuevos gráficos **no están integrados**. La autorización pendiente se
refiere a `../../design/revision-0.6.1/lamina-iconos-logos-v2.png`.
La comparación del componente `portal_assets` con la versión0.6.0 confirma
que se conservan los recursos anteriores. Se restauró el uso de GLaDOS existente.

## Validación

-107/107 escenarios y174 eventos LVGL: lista sin imágenes, preview única,
  colores activos/repetidos, tecladoIP, GLaDOS y regresiones anteriores.
-Once vistas800×480;0 recortes de etiquetas; hashes de fuentes/suite coincidentes.
-Compilación ESP-IDF5.5.6 aprobada; cinco regresiones C++ backend aprobadas.
-38/38 controles seriales en placa; sin fallos de ejecución.
-7/7 comprobaciones reales de metadatos, conectividad y ajustes.

El G-code más reciente de Orca contiene8 posiciones y5 consumos positivos:
T0,T1,T2,T3,T7. PNG300 válido,3539bytes. El panel publica5 activos y omite
T4–T6, cuyo consumo es0. La UI representa solo los colores.

Ajustes actuales conservados: host192.168.0.235:7125, brillo59%, reposo1min,
sonido activado, volumen49%. Este volumen es la lectura actual del usuario,
no el63% de la primera instalación0.6.0. Sin acciones de impresora ni escrituras
de ajustes durante la verificación. Solo se escribió la aplicación en0x10000;
NVS no se flasheó. COM8 quedó cerrado, Klipper en standby y objetivos térmicos0.

Evidencias: `build.log`, `device-inspection.json`, `device-metadata.json`,
`host-action-report.json`, `firmware-manifest.json` y `firmware-0.6.1/`.
El respaldo de la aplicación0.6.0 está en `application-before.bin`.

La eliminación de trabajo por archivo está comprobada en el código y las
pruebas; la mejora percibida de fluidez requiere observación del LCD físico.
La integración de los nuevos iconos/logos se realizará después de la aprobación
de la propuesta gráfica revisada.
