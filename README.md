# Instrucciones

Guia para hablar con un metahuman en Unreal utilizando la API realtime de OpenAI.

## 1. Que incluye este proyecto

El sistema se divide en dos piezas:

- `talk-bot`: bridge local en Node.js que conecta microfono, OpenAI Realtime y Unreal.
- `unreal`: proyecto Unreal.

## 2. Requisitos

### Sistema operativo

- Windows 11

### Herramientas

- `Node.js` reciente con `node` disponible en `PATH`
- `npm`
- `Python 3` disponible en `PATH`
- `Git`

### Unreal y compilacion C++

- `Unreal Engine 5.7`
- `Visual Studio 2022` o compatible con toolchain C++ para Unreal
- Workload de Visual Studio: `Desktop development with C++`
- Componentes:
  - MSVC v143 o superior
  - Windows 111 SDK
  - herramientas de C++ para CMake/MSBuild

### Carpeta content
Extraer la carpeta en el directorio unreal/

### Plugins de Unreal que este proyecto usa

- `LiveLink`
- `LiveLinkControlRig`
- `AppleARKitFaceSupport`
- `PythonScriptPlugin`
- `MetaHuman`
- `MetaHumanCoreTech`
- `MetaHumanLiveLink`

Para instalarlos:
- Doble click sobre unreal/MyProject.uproject
- Si salen mensajes de recompilar o actualizar proyecto dale a Sí.
- En la barra superior ve a Edit->Plugins, busca cada uno de estos y asegúrate de que estén marcados.



### Audio y utilidades externas

- `FFmpeg`

FFmpeg se usa para capturar el microfono desde `bridge/capture-mic.cmd`.
### Servicio externo

- una clave valida en `OPENAI_API_KEY`

## 3. Preparar el bridge

### 3.1 Instalar dependencias

Desde `src/bridge/`:

```powershell
npm install
```

Si `npm` local falla por el entorno de Windows, usa el wrapper incluido:

```powershell
.\npm-local.ps1 install
```

### 3.2 Configurar variables

1. En `src/bridge/.env` rellena al menos:

```env
OPENAI_API_KEY=tu_clave
OPENAI_MODEL=gpt-realtime
OPENAI_VOICE=coral
TB_CONTROL_HOST=127.0.0.1
TB_CONTROL_PORT=8765
TB_INPUT_SAMPLE_RATE=24000
TB_OUTPUT_SAMPLE_RATE=24000
TB_TURN_DETECTION=semantic_vad
```
- Si quieres probar otra voz, cambia OPENAI_VOICE.

### 3.3 Preparar la captura de microfono

Revisa `bridge/capture-mic.cmd`. Este script:

- invoca `ffmpeg.exe`
- abre un dispositivo DirectShow concreto
- convierte el microfono a `PCM16`, mono, `24000 Hz`

Para usarlo has de actualizar:

- la ruta de `ffmpeg.exe`
- el nombre o id del dispositivo de entrada

El id del micro se puede ver con:

```powershell
ruta_a_ffmpeg\ffmpeg-8.1-full_build\bin\ffmpeg.exe -hide_banner -f dshow -list_devices true -i dummy
```

ejemplo:
```powershell
C:\Users\costanahuel\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-8.1-full_build\bin\ffmpeg.exe -hide_banner -f dshow -list_devices true -i dummy
```

### 3.4 Cambiar el pre-prompt del personaje

El pre-prompt que se inyecta antes de cada conversacion vive por defecto en:

- `src/docs/guest-system-prompt.txt`


Otros:

- memoria resumida entre sesiones: `bridge/runtime/memory/` cuando exista
- mezcla final de instrucciones al arrancar: `bridge/src/bridge-app.js`

## 4. Ejecutar el sistema

### 4.1 Arrancar el bridge

Desde `bridge/`:

```powershell
npm start
```

o:

```powershell
node src/index.js
```

Si todo va bien, el bridge:

- abre un WebSocket local en `ws://127.0.0.1:8765`
- conecta con OpenAI Realtime
- empieza a escuchar el microfono
- publica eventos hacia Unreal

La salida debería poner algo como:
```powershell
INFO: Session artifacts will be written to \talk-bot\src\bridge\runtime\sessions\2026-03-27
INFO: OpenAI Realtime voice configured as coral
INFO: Connected to OpenAI Realtime.
INFO: Sent realtime session.update { voice: 'coral', turnDetection: 'semantic_vad' }
INFO: OpenAI session.created default voice alloy
INFO: Starting microphone capture process.
INFO: OpenAI confirmed realtime voice coral
```

### 4.2 Arrancar Unreal

1. Doble click sobre unreal/MyProject.uproject
2. En la parte de abajo hay una sección que pone "Content Drawer", pulsa sobre ella y dale doble click a la carta que pone "inicio". Debería abrir una escena con el metahuman.
3. En la parte de arriba dale al botón de `Play`.

### 4.3 Probar la conversacion

Con el bridge y Unreal en marcha:

1. Habla por el microfono.
2. Espera a que OpenAI responda.
3. Hana deberia:
   - reproducir el audio del asistente
   - mover la cara en realtime
   - mantener una sincronizacion razonable entre boca y voz

## 5. Artefactos y logs

Los artefactos del bridge se guardan en `bridge/runtime/`, normalmente en carpetas de sesion:

- `events.jsonl`
- `transcript.md`
- `assistant-turn-0001.wav`

Esto ayuda a depurar si hay problemas de audio, transcript o lipsync.

Los logs de Unreal suelen quedar en:

- `../unreal/Saved/Logs`

## 6. Problemas habituales

### Unreal no compila porque dice `Live Coding is active`

- cierra Unreal
- vuelve a abrir

### No hay audio del microfono

Revisa:

- la ruta a `ffmpeg.exe`
- el dispositivo configurado en `capture-mic.cmd`
- que `TB_MIC_COMMAND` apunte al script correcto

### Hana habla pero no mueve la cara

Revisa:

- que `Use Realtime MetaHuman Lipsync = true`
- que `Realtime Live Link Subject Name = TalkBotHanaAudio`
- que los plugins `MetaHuman`, `MetaHumanLiveLink` y `LiveLink` esten activos
- que el nivel tenga la Hana correcta y no otra blueprint distinta

### Va lento

Primero prueba con el modo ligero activado. Si aun asi va justo:

- usa `Standalone Game`
- cierra procesos pesados en segundo plano
- baja mas el `Screen Percentage`
- desactiva render costoso mientras pruebas

Si cambias `Apply Lightweight Runtime Mode` a `false` y sigues viendo lo mismo, recuerda que esos cambios se aplican por consola al arrancar `Play` y no restauran automaticamente el estado visual previo. Para comparar bien:

- para `Play`
- vuelve a abrir el editor si hace falta
- entra otra vez con el valor deseado ya fijado antes de pulsar `Play`