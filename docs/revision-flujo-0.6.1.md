# Revisión funcional 0.6.1

Cambios solicitados tras observar0.6.0 en el LCD:

- G-codes a la izquierda: solamente nombres grandes, filas80px y dos líneas.
  Cambiar selección actualiza el resaltado sin reconstruir la lista ni perder
  su desplazamiento. Volver a tocar el mismo archivo ya válido no lo descarga.
- A la derecha: vista previa300×300, colores activos en círculos34px y botón
  Imprimir48px. Se eliminaron tiempos, pesos, cantidades y rótulosT0/T1/etc.
- Se eliminó la caché/precarga de miniaturas por archivo, su API y las copias
  periódicas en la GUI. Listar solicita nombres; solo seleccionar consulta
  metadatos/PNG. La selección tiene prioridad y cancela respuestas antiguas.
- Las copias PNG y la liberación/parseo JSON ocurren fuera del mutex compartido
  de estado. El WebSocket mantiene su tarea independiente.
- IP de Moonraker: teclado numérico con punto, máximo15 caracteres. El puerto
  admite solo dígitos. La consola conserva el soporteDNS anterior.
- GLaDOS original vuelve a espera/completado/cancelado y fallo, con lente de
  estado. La imagen alternativa de G-code se conserva mientras se revisan logos.

Los nuevos iconos y logos requieren autorización previa de la lámina
`design/revision-0.6.1/lamina-iconos-logos.png`; no son parte de este firmware.

Verificación de escritorio:107/107 escenarios,174 eventos LVGL, once vistas
800×480 y0 etiquetas fuera de pantalla. Incluye lista sin imágenes, una sola
vista previa, colores sin cantidades/nombres, colores repetidos, teclado IPv4,
GLaDOS y regresiones anteriores de cierre/inspección/UNKNOWN.

El binario y las pruebas físicas se registran en `verification/ui-flow-update/`.
