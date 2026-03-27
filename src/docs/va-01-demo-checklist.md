# VA-01 Demo conversacional grabable

## Alcance

Esta nota deja el recorrido de demo para el MVP actual, qué quedó validado en local y qué se debe revisar manualmente antes de grabar una toma buena.

## Evidencia validada en esta sesión

- El bridge arranca desde `bridge/src/index.js` y exige `OPENAI_API_KEY`.
- La configuración real del bridge usa por defecto:
  - WebSocket local `ws://127.0.0.1:8765`
  - `gpt-realtime`
  - audio PCM16 mono a `24000 Hz`
  - buffer inicial de respuesta `250 ms`
- El código del bridge pasa verificación sintáctica con `node --check src/index.js`.
- Hay artefactos reales de conversación ya generados en `bridge/runtime/sessions/2026-03-26_09-14-42/`:
  - `assistant-turn-0001.wav` con duración aproximada de `7.10 s`
  - `assistant-turn-0002.wav` con duración aproximada de `8.35 s`
  - `events.jsonl`
  - `transcript.md`
- Los logs guardados muestran que el flujo `input_text -> respuesta de audio -> WAV final` sí ocurrió.
- A partir de los timestamps de `events.jsonl`, el primer audio del asistente apareció aproximadamente:
  - `560 ms` después del primer `input_text`
  - `356 ms` después del segundo `input_text`

Estas latencias son una inferencia a partir de los logs del bridge, no una medición oficial de Unreal ni de MetaHuman.

## Incidencias detectadas

- Los textos persistidos en `transcript.md` y `events.jsonl` presentan mojibake en español (`presÃ©ntate`, `AquÃ­`, `quÃ©`).
- Desde terminal no puedo validar la sincronía audiovisual percibida del MetaHuman ni la captura real en OBS/móvil; esa parte sigue siendo una comprobación manual en la máquina de demo.

## Recorrido de demo recomendado

1. Preparar Unreal con el MetaHuman y el cliente local conectable al bridge.
2. Confirmar que `bridge/.env` contiene al menos `OPENAI_API_KEY`.
3. Arrancar el bridge desde `bridge/` con `node src/index.js` o `npm start`.
4. Verificar que el cliente local recibe `bridge.ready` y luego `session.created`.
5. Lanzar una prueba corta con un prompt de texto antes de grabar.
6. Confirmar que Unreal reproduce la voz del invitado y mueve la cara con el mismo audio.
7. Empezar la grabación solo cuando la ruta completa ya esté estable.

## Script mínimo de conversación

Usar tres turnos cortos y fáciles de juzgar:

1. "Preséntate como invitado del podcast en una frase corta."
2. "Cuéntame una anécdota divertida en dos frases."
3. "Despídete mirando a cámara con una frase energética."

## Criterios de validación manual

- [ ] El bridge crea una nueva carpeta dentro de `bridge/runtime/sessions/`.
- [ ] Unreal se conecta al WebSocket local sin errores visibles.
- [ ] El invitado responde con voz audible y sin cortes graves.
- [ ] La boca del MetaHuman arranca cerca del inicio de la voz y termina cerca del final.
- [ ] No se aprecia drift claro entre voz y labios durante frases de 5-10 segundos.
- [ ] El turno completo deja `assistant-turn-XXXX.wav`, `events.jsonl` y `transcript.md`.
- [ ] La transcripción final coincide razonablemente con lo que se oye.
- [ ] No hay clipping, eco evidente ni doble reproducción de audio.

## Checklist mínimo para grabar con OBS

- [ ] Escena única con la ventana/cámara correcta de Unreal.
- [ ] Fuente de audio del escritorio limitada a la salida final de Unreal.
- [ ] Micrófono del presentador activo y con nivel estable.
- [ ] Grabación de prueba de 10 segundos revisada antes de la toma buena.
- [ ] Resolución y FPS fijados antes de empezar la demo.
- [ ] Notificaciones del sistema silenciadas.

## Checklist mínimo para grabar con móvil

- [ ] Móvil en horizontal y soporte estable.
- [ ] Encuadre que capture bien al MetaHuman y, si aplica, al host.
- [ ] Prueba corta para confirmar que la voz del invitado se entiende.
- [ ] Brillo y reflejos del monitor controlados.
- [ ] Cargador o batería suficiente para varias tomas.

## Recomendación de pase final antes de cerrar VA-01

Hacer una toma manual completa con Unreal abierto y grabación activa. Si la sincronía visual se ve creíble y el audio queda limpio, VA-01 puede pasar a revisión humana con una única incidencia abierta: corregir la codificación UTF-8 de los artefactos de sesión.
