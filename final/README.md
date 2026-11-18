# Proyecto ESP-IDF

Consulta el [README del repositorio](../README.md) para ver el diseño, las funciones y los requisitos del panel.

- `main/` inicia el firmware.
- `components/` contiene la interfaz LVGL, el cliente Klipper, la red, el audio y los controladores del panel.
- `assets/`, `IMAGENES/` y `design/revision-0.6.1/` contienen los recursos gráficos y sus fuentes.
- `tests/` y `tools/` contienen las pruebas, el simulador y las herramientas de compilación.

La [guía de pruebas LVGL](tests/portal_host/README.md) explica cómo ejecutar el simulador desde esta carpeta.

## Lectura de microSD

Para activar los núcleos HD, copia la carpeta [animaciones](assets/portal/cores-hd/animaciones/)
a la raíz de una microSD FAT32 e inserta la tarjeta en el panel. La ruta de
ejemplo para reposo es `animaciones/prueba_core/lz4/pensativo.pcore`.
Cada paquete LZ4 contiene 72 fotogramas de 300 × 300 px y se reproduce
a 12 fps, con una vuelta de seis segundos.

Con el panel conectado al Wi-Fi, puedes probar la lectura de PNG sueltos con
`sd test` en la consola USB. El diagnóstico monta la tarjeta, cuenta las imágenes
de `animaciones/codigo/`, lee el primer PNG y desmonta la tarjeta.

`SD_READ` muestra tamaño, dimensiones y CRC32 del archivo leído. En `SD_TEST`, las
tres operaciones deben indicar `ESP_OK` y `wifi_before`, `wifi_mounted` y
`wifi_after` deben indicar `1` para verificar la conexión antes, durante y después
del acceso a la tarjeta.

Para usar PNG sueltos, coloca 72 fotogramas consecutivos de 300 × 300 px en una
carpeta bajo `animaciones/` con el nombre correspondiente. Por ejemplo,
`animaciones/pensativo/pensativo_0001.png`.

| Estado | Carpeta | Dónde aparece |
| --- | --- | --- |
| Desconectado | `fallando` | Vista de estado y salvapantallas |
| Impresión completada | `feliz` | Vista de estado y salvapantallas |
| Error de Klipper | `indignado` | Vista de error y salvapantallas |
| En pausa | `pausa` | Salvapantallas |
| Imprimiendo | `imprimiendo` | Salvapantallas |
| Reposo | `pensativo` | Reposo y salvapantallas |

El lector también admite secuencias agrupadas por el nombre de la animación
al inicio de cada PNG. Las secuencias anteriores `reposo` y `codigo` sirven
como reserva de Pensativo e Imprimiendo si falta la correspondiente.

El panel carga en PSRAM el clip correspondiente al estado de la impresora. El
core integrado se muestra durante la carga, si falta un clip o si hay un error.
`core scan` comprueba por USB las secuencias de PNG sueltos y sus dimensiones.
`core status` indica el formato y el clip cargados, además de cuántos cuadros
y vueltas se reprodujeron.

El salvapantallas reproduce los mismos fotogramas con una mezcla del 40 % de
negro aplicada a la imagen del core. El control de retroiluminación sigue
funcionando de forma independiente.
