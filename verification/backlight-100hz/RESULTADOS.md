# Prueba de retroiluminación a 100 Hz

2026-10-27, America/Bogota. Firmware **0.6.5-backlight-100hz** cargado en COM8,
ESP32-P4 v1.3, identidad USB `80:F1:B2:D1:7F:0B`.

## Cambio y motivo

El usuario informó que la prueba a 1 kHz no eliminó el zumbido: solo desaparece
al 100 % de brillo. Aclara que pudo haber sido constante antes de esa prueba.
Pidió probar **100 Hz**, como en la configuración de espControl consultada.

En GPIO23 se cambia la frecuencia PWM de 1000 a **100 Hz**. Se conservan la
resolución de 10 bits, el cálculo del brillo y el apagado por inactividad.
No se modifica la configuración del audio.

## Compilación y carga

- ESP-IDF **5.5.6**, compilación completada sin advertencias ni errores.
- Aplicación: **3.075.040 bytes**; partición de 4 MiB, **27 % libre**.
- Se escribió solo la aplicación en `0x10000`; esptool verificó su hash.
- No se escribió NVS, el cargador de arranque ni la tabla de particiones.
- Respaldo de la aplicación anterior a 1 kHz: `application-before.bin`.
- Binarios y configuración: `firmware-0.6.5/`.

SHA-256 de la aplicación cargada:

```text
8e763fe395ae557dd81b2dd3b4e2b66fb06afb7fbe544b19cdf0cb3ad2609232
```

La verificación de arranque y ajustes se registra en `after-device.log` y
`after-device.json`: **8/8 controles aprobados**. La consola confirmó
`Backlight PWM: GPIO23, 100 Hz, 10 bits`, versión 0.6.5, arranque de PORTAL,
registro del táctil, códec listo y conexión con Moonraker, sin errores detectados.
Se conservaron brillo 5 %, volumen 89 %, sonido activado y reposo de 1 minuto,
iguales a la lectura anterior a la carga. El comprobador no modifica ajustes ni
envía órdenes a la impresora; cierra COM8 al terminar.

**Resultado acústico confirmado por el usuario: zumbido solucionado a 100 Hz.**
Respuesta recibida: «solucionado, gracias». Ver `user-observation.json`.
