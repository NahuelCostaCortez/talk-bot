# Bridge local Realtime

Este directorio contiene el bridge local del MVP para:

- capturar audio de usuario
- conectar con OpenAI Realtime por WebSocket
- reenviar audio/eventos a clientes locales como Unreal
- guardar artefactos de sesion en disco

## Estado actual

El bridge esta implementado en Node.js con JavaScript CommonJS para reducir friccion del entorno actual. La dependencia externa minima es `ws`.

En esta maquina, `npm` apunta a una instalacion vieja de Scoop fuera del workspace, asi que no he podido instalar dependencias ni ejecutar el bridge end-to-end desde esta sesion. El codigo queda preparado para ejecutarse en cuanto el toolchain de Node quede sano.

## Estructura

- `src/openai-realtime-client.js`: conexion con OpenAI Realtime
- `src/local-control-server.js`: WebSocket local para clientes como Unreal
- `src/process-audio-input.js`: adaptador de microfono via comando externo
- `src/process-audio-output.js`: adaptador de altavoces via comando externo
- `src/file-session-store.js`: persistencia local de logs, transcripts y wavs
- `src/bridge-app.js`: orquestacion principal

## Configuracion

1. Copia `.env.example` a `.env` o exporta variables de entorno.
2. Define `OPENAI_API_KEY`.
3. Opcionalmente define:
   - `OPENAI_VOICE=coral` para una voz femenina por defecto
   - `TB_MIC_COMMAND`: comando que emita PCM16 mono 24kHz a `stdout`
   - `TB_SPEAKER_COMMAND`: comando que acepte PCM16 mono 24kHz por `stdin`

## npm local de emergencia

Esta maquina tiene `npm` roto por una ruta vieja de Scoop en otro perfil de usuario. Para evitarlo, usa:

```powershell
.\npm-local.ps1 install
```

Eso ejecuta el CLI de `npm` copiado en `tools/npm` y usa cache local dentro del workspace.

## Ejemplos de integracion

El bridge expone un WebSocket local en `ws://127.0.0.1:8765` por defecto.

Mensajes salientes principales:

- `bridge.ready`
- `session.created`
- `assistant.response_started`
- `assistant.audio_chunk`
- `assistant.response_finished`
- `assistant.lipsync_started`
- `assistant.lipsync_completed`
- `assistant.lipsync_failed`
- `assistant.transcript_final`
- `bridge.warning`
- `bridge.error`

Mensajes entrantes soportados:

- `ping`
- `input_text`
- `response.create`

## Siguiente paso

Cuando el entorno de Node este operativo, la siguiente prueba util es:

1. `.\npm-local.ps1 install`
2. configurar `OPENAI_API_KEY`
3. arrancar el bridge
4. inyectar un `input_text` por el WebSocket local para validar la ruta OpenAI -> bridge -> cliente local

## Hook de lipsync runtime

Si defines `TB_LIPSYNC_COMMAND`, el bridge lanza ese comando automaticamente al terminar cada respuesta del asistente, usando el `wavPath` final como entrada.

Variables disponibles para el comando:

- `TB_LIPSYNC_WAV_PATH`
- `TB_LIPSYNC_TURN_ID`
- `TB_LIPSYNC_TRANSCRIPT`
- `TB_LIPSYNC_SESSION_DIR`
- `TB_LIPSYNC_SAMPLE_RATE`

Tambien puedes ajustar `TB_LIPSYNC_TIMEOUT_MS` para limitar la duracion maxima del proceso externo.

Detalle de la integracion esperada con Hana: `bridge/docs/hana-runtime-lipsync.md`

Guia completa de instalacion y arranque del proyecto: `docs/project-setup-and-usage.md`
