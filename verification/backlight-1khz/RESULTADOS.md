# Prueba de retroiluminación a 1 kHz

2026-10-27, America/Bogota. Firmware **0.6.4-backlight-1khz** compilado y
cargado en COM8, ESP32-P4 v1.3, identidad USB `80:F1:B2:D1:7F:0B`.

## Cambio

La frecuencia PWM de GPIO23 pasa de 5000 a **1000 Hz**. Conserva la resolución
de 10 bits, el cálculo del brillo y el apagado por inactividad. Se añade al
arranque una lectura de la frecuencia configurada mediante `ledc_get_freq`.
El controlador MP3202 del esquema recibe PWM por EN; su ficha recomienda
1 kHz o menos para este método de regulación.

## Compilación y carga

- ESP-IDF **5.5.6**, compilación completada sin advertencias ni errores.
- Aplicación: **3.075.040 bytes**; partición de 4 MiB, **27 % libre**.
- Se escribió únicamente la aplicación en `0x10000`; esptool verificó su hash.
- No se escribió NVS, el cargador de arranque ni la tabla de particiones.
- `application-before.bin` conserva la aplicación anterior, 0.6.3.
- Binarios y configuración: `firmware-0.6.4/`; huellas: `firmware-manifest.json`.

SHA-256 de la aplicación cargada:

```text
7eb8927f0cd374f7f83cb2a3f9adc3c008467db271d09c4bc028796acdb94a54
```

## Comprobación física por consola

El registro confirma la versión nueva y **`Backlight PWM: GPIO23, 1000 Hz, 10 bits`**.
Arrancaron PORTAL, el táctil y el códec ES8311; se recuperaron Wi-Fi y Moonraker.
No se detectaron brownout, panic, assert ni timeout de pantalla en los registros.
La última consulta fue de lectura y dejó COM8 cerrado.

La primera lectura tras cargar conserva brillo **5 %**, volumen **96 %**, sonido
activado y reposo **1 minuto**, iguales a los valores anteriores. La comparación
automática posterior encontró valores diferentes durante la prueba: el usuario
confirmó expresamente que estaba cambiando brillo y audio. Se conservaron sus
cambios. Esa diferencia figura en `after-device.json`; no implica pérdida de NVS.
La consulta posterior sin reinicio pasó sus tres controles en `live-device.json`.

La primera ejecución del comprobador adelantó la lectura por un prompt de arranque
pendiente; sus datos están en `after-device-attempt1.log`. Se corrigió la
sincronización para esperar las respuestas `status` y `AUDIO_STATUS`.

No se enviaron órdenes a la impresora ni cambios de ajustes desde el comprobador.
**La desaparición del zumbido y la ausencia de parpadeo requieren observación del usuario.**

Observación posterior del usuario: el zumbido persiste a 1 kHz y solo desaparece
al 100 % de brillo. El usuario aclara que pudo haber estado presente de forma
constante antes de esta prueba; no se atribuye un empeoramiento a 1 kHz.
Solicitó comparar con **100 Hz**; ver `../backlight-100hz/RESULTADOS.md`.
