# Modelo de Repositorios y Guía para Pull Requests al Cliente

Este documento describe la arquitectura de repositorios del proyecto PlotJuggler Core y establece las reglas para enviar contribuciones desde el entorno de desarrollo interno hacia el repositorio público del cliente.

---

## 1. Arquitectura de Repositorios

El proyecto mantiene dos repositorios remotos con propósitos diferenciados:

| Remote | Ubicación | Propósito |
|--------|-----------|-----------|
| `ibrobotics` | GitLab interno | Desarrollo interno del equipo. Contiene trabajo en progreso, experimentos y referencias internas. |
| `plotjuggler` | GitHub público | Repositorio del cliente. Código limpio y listo para producción. |

### 1.1 Ramas Principales

```
ibrobotics/internal_main    →    plotjuggler/development
     (interno)                        (cliente)
```

- **`internal_main`** (remote `ibrobotics`): Rama principal de desarrollo interno. Puede contener commits con referencias a herramientas internas, personal del equipo o mensajes de commit informales.

- **`development`** (remote `plotjuggler`): Rama principal del cliente. Todo el código debe estar limpio, bien documentado y sin referencias internas.

---

## 2. Reglas para Pull Requests al Cliente

### 2.1 Preparación del Código

Antes de crear una PR hacia el repositorio del cliente, el código debe cumplir los siguientes requisitos:

#### Limpieza de Contenido

- **Sin referencias a herramientas de IA**: No incluir menciones a Claude, ChatGPT, Copilot ni ningún asistente de código en commits ni comentarios.
- **Sin referencias a personal interno**: No incluir nombres de empleados internos en los commits (excepto el autor designado).
- **Sin referencias a commits internos**: El mensaje de commit no debe mencionar hashes, ramas ni historial del repositorio interno.
- **Sin archivos innecesarios**: Eliminar archivos temporales, duplicados o que no aporten valor (ej: `archivo (2).png`, `backup_*.txt`).

#### Autor del Commit

Todos los commits deben usar un autor uniforme:

```
Nombre: Pablo Iñigo Blasco
Email: pablo.inigo@ibrobotics.com
```

### 2.2 Proceso de Creación de PR

#### Paso 1: Actualizar los Remotes

```bash
git fetch --all
```

#### Paso 2: Crear una Rama Descriptiva

El nombre de la rama debe describir la funcionalidad, **nunca** indicar que es una sincronización interna.

**Correcto:**
```bash
git checkout -b feature/marketplace-specification plotjuggler/development
git checkout -b fix/memory-leak-datastore plotjuggler/development
git checkout -b docs/api-reference plotjuggler/development
```

**Incorrecto:**
```bash
git checkout -b sync/internal-to-client        # ❌ Revela proceso interno
git checkout -b merge/from-internal-main       # ❌ Revela proceso interno
```

#### Paso 3: Squash Merge

Consolidar todos los commits internos en un único commit limpio:

```bash
git merge --squash ibrobotics/internal_main
```

#### Paso 4: Revisar y Limpiar

Antes de hacer commit, revisar los archivos staged:

```bash
git status
```

Eliminar archivos innecesarios:

```bash
git rm --cached "ruta/archivo_innecesario.png"
rm -f "ruta/archivo_innecesario.png"
```

#### Paso 5: Crear el Commit

Usar variables de entorno para garantizar autor y committer uniformes:

```bash
GIT_COMMITTER_NAME="Pablo Iñigo Blasco" \
GIT_COMMITTER_EMAIL="pablo.inigo@ibrobotics.com" \
git commit --author="Pablo Iñigo Blasco <pablo.inigo@ibrobotics.com>" \
-m "$(cat <<'EOF'
tipo: descripción breve del cambio

Descripción detallada de los cambios realizados.
Explicar el qué y el por qué, no el cómo.

Contenido:
- Lista de cambios principales
- Archivos o módulos afectados
EOF
)"
```

#### Paso 6: Push y Crear PR

```bash
git push -u plotjuggler nombre-de-rama
```

Crear la PR manualmente en GitHub o con `gh`:

```bash
gh pr create --repo PlotJuggler/plotjuggler_core \
  --base development \
  --title "Título descriptivo" \
  --body "Descripción de la PR"
```

---

## 3. Reglas Durante la Revisión

### 3.1 No Usar Force Push

Una vez que la PR está creada y en revisión, **nunca** usar `git push --force`. Esto causa:

- Pérdida del historial de revisiones
- Conflictos para revisores que ya descargaron la rama
- Comentarios de revisión desvinculados de los commits

**En su lugar:** Crear commits adicionales para correcciones. El historial se puede limpiar al hacer merge si es necesario.

### 3.2 Commits de Corrección

Si hay que hacer cambios durante la revisión:

```bash
# Hacer los cambios necesarios
git add -A
GIT_COMMITTER_NAME="Pablo Iñigo Blasco" \
GIT_COMMITTER_EMAIL="pablo.inigo@ibrobotics.com" \
git commit --author="Pablo Iñigo Blasco <pablo.inigo@ibrobotics.com>" \
-m "fix: corregir problema detectado en revisión"

# Push normal (sin --force)
git push
```

---

## 4. Estructura del Mensaje de Commit

### 4.1 Formato

```
<tipo>: <descripción breve>

<descripción detallada (opcional)>

<contenido/cambios (opcional)>
```

### 4.2 Tipos de Commit

| Tipo | Uso |
|------|-----|
| `feat` | Nueva funcionalidad |
| `fix` | Corrección de bug |
| `docs` | Documentación |
| `refactor` | Refactorización sin cambio de comportamiento |
| `test` | Tests |
| `ci` | Cambios en CI/CD |
| `chore` | Tareas de mantenimiento |

### 4.3 Ejemplo

```
docs: add PlotJuggler Marketplace technical specification v1.0.0

Add comprehensive technical specification for the PlotJuggler Marketplace,
an extension distribution system inspired by VSCode's model.

Key features documented:
- Serverless architecture using GitHub for registry and artifact hosting
- Cross-platform plugin distribution (Linux, Windows, macOS)
- ABI-compatible plugin SDK (Qt-free plugins)

Contents:
- Technical specification document (v1.0.0)
- PlantUML diagrams: architecture, installation flow, rollback flow
```

---

## 5. Checklist Pre-PR

Antes de crear una PR, verificar:

- [ ] Rama con nombre descriptivo (sin referencias a "sync" o "internal")
- [ ] Squash merge realizado (un único commit)
- [ ] Autor y committer con email `pablo.inigo@ibrobotics.com`
- [ ] Sin referencias a Claude ni otras herramientas de IA
- [ ] Sin referencias a personal interno ni commits internos
- [ ] Archivos innecesarios eliminados (duplicados, temporales)
- [ ] Mensaje de commit descriptivo y profesional
- [ ] Archivos generados incluidos (ej: PNGs de diagramas PUML)

---

## 6. Resumen Visual

```
┌─────────────────────────────────────────────────────────────────┐
│                    REPOSITORIO INTERNO                          │
│                    (ibrobotics/internal_main)                   │
│                                                                 │
│  commits internos, WIP, referencias a herramientas, etc.        │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ git merge --squash
                              │ + limpieza
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    RAMA LOCAL                                   │
│                    (feature/nombre-descriptivo)                 │
│                                                                 │
│  único commit limpio, autor uniforme, sin referencias internas  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              │ git push (sin --force)
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                    REPOSITORIO CLIENTE                          │
│                    (plotjuggler/development)                    │
│                                                                 │
│  código limpio, profesional, listo para producción              │
└─────────────────────────────────────────────────────────────────┘
```

---

*Documento creado: 2026-03-05*
*Última actualización: 2026-03-05*
