# Hana runtime lipsync

El bridge ya emite `assistant.response_finished` con `wavPath`, pero para `IN-03` conviene que tambien pueda disparar automaticamente el postproceso de lipsync al cerrar cada turno.

## Activacion

Define `TB_LIPSYNC_COMMAND` en `bridge/.env` con el comando que deba procesar el WAV final y dejar lista la reproduccion sobre la Hana del nivel.

Ejemplo con PowerShell:

```powershell
TB_LIPSYNC_COMMAND=powershell -ExecutionPolicy Bypass -File C:\Ruta\RunTalkBotHanaRuntime.ps1
```

## Variables disponibles

El bridge lanza ese comando con estas variables de entorno:

- `TB_LIPSYNC_WAV_PATH`: ruta absoluta del WAV final del turno
- `TB_LIPSYNC_TURN_ID`: indice del turno del asistente
- `TB_LIPSYNC_TRANSCRIPT`: transcript final si existe
- `TB_LIPSYNC_SESSION_DIR`: carpeta de sesion en `bridge/runtime/sessions/...`
- `TB_LIPSYNC_SAMPLE_RATE`: sample rate de salida del bridge

## Contrato recomendado para Unreal

El comando de lipsync deberia:

1. importar o reutilizar el `SoundWave` generado desde `TB_LIPSYNC_WAV_PATH`
2. generar o cargar la `LevelSequence` facial correspondiente
3. rebinding de la secuencia sobre la instancia ya colocada de Hana en el nivel
4. reproducir audio y animacion desde un mismo punto de control en Play

La parte clave de `IN-03` es el punto 3: evitar un MetaHuman duplicado o spawnable y convertir la secuencia para que posea a la Hana ya existente en la escena.

## Eventos extra del bridge

Cuando la automatizacion esta activa, el bridge emite tambien:

- `assistant.lipsync_started`
- `assistant.lipsync_completed`
- `assistant.lipsync_failed`

Eso permite que el cliente Unreal o una herramienta auxiliar muestren estado y errores sin depender solo de logs del proceso externo.
