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

Desde la raíz del repositorio, entrar a `final/` para compilar con
`./tools/idf.ps1 build`. Git versiona el código, los recursos, las pruebas,
la documentación vigente y la configuración. ESP-IDF recupera las
dependencias mediante `final/dependencies.lock`.

La carpeta local `no nesesario/` guarda las referencias y los respaldos.
El PowerPoint editable está en `no nesesario/entregables/`. En `final/IMAGENES/`
quedan las cuatro entradas del generador: `WIFI.png`, `aperture inicio.png`,
`fondo.png` y `klipper.png`. Los cores que usa el generador están en
`final/assets/portal/cores/`.

Configurar Wi-Fi y Moonraker en el dispositivo. Mantener contrasenas, tokens,
archivos `.env` y volcados de NVS fuera de los commits. Conservar las licencias
y la procedencia de recursos de terceros incluidas en sus carpetas.
