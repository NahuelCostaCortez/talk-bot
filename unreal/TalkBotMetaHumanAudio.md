# TalkBot MetaHuman Audio

## Goal

Turn a real bridge WAV from TalkBot into:

- a `SoundWave` asset
- a processed `MetaHuman Performance`
- an exported `Anim Sequence`
- an exported `Level Sequence` targeted to `BP_Hana`

## What was added

- Enabled `MetaHumanAnimator`
- Enabled `PythonScriptPlugin`
- Added project script:
  - `Scripts/talkbot_audio_to_metahuman.py`
- Added wrapper command:
  - `RunTalkBotAudioToMetaHuman.ps1`

## Easiest way to use it

1. Close Unreal.
2. Generate a response with the bridge so a fresh `assistant-turn-XXXX.wav` exists.
3. Run in PowerShell:

```powershell
cd "C:\Users\costanahuel\Documents\Unreal Projects\MyProject"
.\RunTalkBotAudioToMetaHuman.ps1 -Latest
```

4. Open Unreal again.
5. Look under:
   - `/Game/TalkBot/Audio`
   - `/Game/TalkBot/Performance`
   - `/Game/TalkBot/Sequences`

## Expected output

For a bridge wav, the script creates:

- `TB_<session>_<turn>` `SoundWave`
- `TB_<session>_<turn>_Performance`
- `AS_TB_<session>_<turn>`
- `LS_TB_<session>_<turn>_Hana`

The level sequence is the most useful output for previewing the solved face animation on `BP_Hana`.

## Notes

- This flow prioritizes believable sync over minimum latency.
- It is designed around completed assistant utterances, not chunk-by-chunk live lipsync.
- If you want a more lightweight solve, rerun with `-ProcessMask MouthOnly`.
