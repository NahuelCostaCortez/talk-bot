# TalkBot Bridge Setup

## What was added

- A runtime C++ module for `MyProject`
- `ATalkBotBridgeActor`, a Blueprintable actor that:
  - connects to `ws://127.0.0.1:8765`
  - receives `assistant.audio_chunk` and plays it in Unreal
  - emits Blueprint events for:
    - bridge connected / ready
    - session created
    - assistant response started
    - assistant transcript final
    - assistant response finished with `wavPath`

## Recommended first use

1. Open the project in Unreal 5.7.
2. If Unreal asks to rebuild modules, accept.
3. Create a Blueprint subclass of `TalkBotBridgeActor`.
4. Place that Blueprint in the level near the MetaHuman, or attach it to the avatar flow you already use.
5. Keep `bAutoConnect = true`.
6. Press Play with the local bridge running.

## Blueprint events to use

- `OnBridgeReady`
- `OnSessionCreated`
- `OnAssistantResponseStarted`
- `OnAssistantTranscriptFinal`
- `OnAssistantResponseFinished`

`OnAssistantResponseFinished` is the key handoff for the next task because it includes:

- `turnId`
- `transcript`
- `wavPath`

That lets us keep `IN-01` focused on transport and playback, while `IN-02` can consume the finished WAV for MetaHuman ADA.

## Audio behavior

- Audio is played in Unreal through a `USoundWaveProcedural`
- The audio component can be obtained from `GetAudioComponent()`
- This keeps the final playback inside Unreal instead of the bridge

## Current limitation

Local build verification from command line is currently blocked on this machine because UnrealBuildTool reports missing or invalid `Win64` platform support / SDK.
