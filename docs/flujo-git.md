# Trabajo con Git y GitHub

El repositorio usa dos ramas permanentes:

- `main`: base estable del firmware. Integrar aqui los cambios revisados y validados.
- `develop`: integracion del trabajo en curso. Crear las ramas de cada cambio desde aqui.

La importacion inicial parte de la misma instantanea en ambas ramas. Los informes
historicos de hardware documentan las comprobaciones ya realizadas; la publicacion
en GitHub no implica una nueva carga ni una nueva validacion en placa.

## Empezar un cambio

Con el directorio de trabajo limpio:

```powershell
git switch develop
git pull --ff-only
git switch -c feature/nombre-del-cambio
```

Usar `feature/...` para funciones nuevas, `fix/...` para correcciones y `docs/...`
para documentacion. Cada rama debe contener un cambio concreto.

## Guardar y publicar el cambio

Revisar el diff y agregar explicitamente los archivos del cambio:

```powershell
git status
git diff
git add ruta/al/archivo
git diff --cached
git commit -m "Describe el cambio"
git push -u origin HEAD
```

Abrir un pull request hacia `develop`, indicando el comportamiento resultante
y las comprobaciones realizadas. Para una entrega estable, abrir un pull request
de `develop` hacia `main`. Etiquetar una version solo cuando corresponda al
firmware comprobado. Los binarios de una entrega pueden adjuntarse a GitHub
Releases, fuera del historial del codigo.

## Archivos locales

Se versionan fuentes, recursos, manifiestos de dependencias, configuracion de
compilacion, documentacion e informes/capturas seleccionados de `verification/`.
Se excluyen `build/`, `managed_components/`, `.cache/`, ajustes del editor,
salidas del simulador, logs de dispositivo, binarios y respaldos de verificacion.
Las dependencias administradas se recuperan mediante ESP-IDF y `dependencies.lock`.
Los archivos excluidos permanecen en el equipo; algunas rutas de evidencia
historica mencionadas en los informes solo estan disponibles localmente.

Configurar Wi-Fi y Moonraker en el dispositivo. Mantener contrasenas, tokens,
archivos `.env` y volcados de NVS fuera de los commits. Conservar las licencias
y la procedencia de recursos de terceros incluidas en sus carpetas.
