import argparse
import re
import sys
from pathlib import Path

import unreal


DEFAULT_META_HUMAN_PATH = "/Game/MetaHumans/Hana/BP_Hana.BP_Hana"
DEFAULT_BRIDGE_RUNTIME = r"C:\Users\costanahuel\Desktop\talk-bot\bridge\runtime\sessions"
DEFAULT_STORAGE_ROOT = "/Game/TalkBot"


def ensure_epic_metahuman_python_path():
    plugin_python_dir = (
        Path(unreal.Paths.engine_plugins_dir())
        / "MetaHuman"
        / "MetaHumanAnimator"
        / "Content"
        / "Python"
    )
    plugin_python_path = str(plugin_python_dir)
    if plugin_python_path not in sys.path:
        sys.path.append(plugin_python_path)


ensure_epic_metahuman_python_path()

from export_performance import run_anim_sequence_export, run_meta_human_level_sequence_export  # noqa: E402
from process_audio_performance import create_performance_asset  # noqa: E402
from process_performance import process_shot  # noqa: E402


def sanitize_asset_name(value: str) -> str:
    sanitized = re.sub(r"[^A-Za-z0-9_]+", "_", value)
    sanitized = sanitized.strip("_")
    return sanitized or "TalkBotAudio"


def find_latest_wav(runtime_sessions_dir: Path) -> Path:
    wav_files = list(runtime_sessions_dir.glob("**/assistant-turn-*.wav"))
    if not wav_files:
        raise RuntimeError(f"No bridge wav files found under {runtime_sessions_dir}")
    return max(wav_files, key=lambda item: item.stat().st_mtime)


def build_asset_stem(wav_path: Path) -> str:
    session_name = wav_path.parent.name
    return sanitize_asset_name(f"TB_{session_name}_{wav_path.stem}")


def import_sound_wave(wav_path: Path, destination_path: str, destination_name: str) -> str:
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    import_task = unreal.AssetImportTask()
    import_task.filename = str(wav_path)
    import_task.destination_path = destination_path
    import_task.destination_name = destination_name
    import_task.automated = True
    import_task.save = True
    import_task.replace_existing = True
    import_task.replace_existing_settings = True

    asset_tools.import_asset_tasks([import_task])
    imported_object_paths = list(import_task.imported_object_paths)
    if not imported_object_paths:
        raise RuntimeError(f"Failed to import {wav_path} as a SoundWave")

    soundwave_path = imported_object_paths[0]
    unreal.log(f"Imported SoundWave: {soundwave_path}")
    return soundwave_path


def save_asset_if_loaded(asset) -> None:
    if not asset:
        return

    editor_asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    editor_asset_subsystem.save_loaded_asset(asset, False)


def process_audio_to_metahuman(
    wav_path: Path,
    meta_human_path: str,
    storage_root: str,
    *,
    mood: str,
    mood_intensity: float,
    process_mask: str,
    head_movement_mode: str,
):
    asset_stem = build_asset_stem(wav_path)
    audio_path = f"{storage_root}/Audio"
    performance_path = f"{storage_root}/Performance"
    sequence_path = f"{storage_root}/Sequences"

    soundwave_asset_path = import_sound_wave(
        wav_path=wav_path,
        destination_path=audio_path,
        destination_name=asset_stem,
    )

    performance_asset = create_performance_asset(
        path_to_sound_wave=soundwave_asset_path,
        save_performance_location=performance_path,
        asset_name=f"{asset_stem}_Performance",
        mood=mood,
        mood_intensity=mood_intensity,
        process_mask=process_mask,
        head_movement_mode=head_movement_mode,
    )
    save_asset_if_loaded(performance_asset)

    process_shot(performance_asset=performance_asset)
    save_asset_if_loaded(performance_asset)

    anim_sequence = run_anim_sequence_export(
        performance_asset=performance_asset,
        export_sequence_location=sequence_path,
        export_sequence_name=f"AS_{asset_stem}",
    )
    save_asset_if_loaded(anim_sequence)

    level_sequence = run_meta_human_level_sequence_export(
        performance_asset=performance_asset,
        target_meta_human_path=meta_human_path,
        export_sequence_location=sequence_path,
        export_sequence_name=f"LS_{asset_stem}_Hana",
    )
    save_asset_if_loaded(level_sequence)

    unreal.log("TalkBot MetaHuman processing complete")
    unreal.log(f"WAV source: {wav_path}")
    unreal.log(f"SoundWave: {soundwave_asset_path}")
    unreal.log(f"Performance: {performance_asset.get_path_name() if performance_asset else 'n/a'}")
    unreal.log(f"Anim Sequence: {anim_sequence.get_path_name() if anim_sequence else 'n/a'}")
    unreal.log(f"Level Sequence: {level_sequence.get_path_name() if level_sequence else 'n/a'}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Import a TalkBot bridge wav, solve MetaHuman audio-driven animation, and export sequence assets."
    )
    parser.add_argument("--wav-path", type=str, default="", help="Absolute path to a bridge wav file")
    parser.add_argument("--latest", action="store_true", help="Use the newest bridge wav under the runtime sessions folder")
    parser.add_argument("--bridge-runtime-dir", type=str, default=DEFAULT_BRIDGE_RUNTIME, help="Root runtime sessions folder")
    parser.add_argument("--meta-human-path", type=str, default=DEFAULT_META_HUMAN_PATH, help="Content path to the target MetaHuman BP asset")
    parser.add_argument("--storage-root", type=str, default=DEFAULT_STORAGE_ROOT, help="Content path root for imported audio and generated assets")
    parser.add_argument("--mood", type=str, default="Auto", choices=["Auto", "Neutral", "Happy", "Sad", "Disgust", "Anger", "Surprise", "Fear"])
    parser.add_argument("--mood-intensity", type=float, default=1.0)
    parser.add_argument("--process-mask", type=str, default="FullFace", choices=["FullFace", "MouthOnly"])
    parser.add_argument("--head-movement-mode", type=str, default="ControlRig", choices=["ControlRig", "TransformTrack", "Disabled"])
    return parser.parse_args()


def main():
    args = parse_args()
    if not args.wav_path and not args.latest:
        raise RuntimeError("Pass --wav-path or --latest")

    if args.wav_path:
        wav_path = Path(args.wav_path)
    else:
        wav_path = find_latest_wav(Path(args.bridge_runtime_dir))

    if not wav_path.exists():
        raise RuntimeError(f"WAV path does not exist: {wav_path}")

    process_audio_to_metahuman(
        wav_path=wav_path,
        meta_human_path=args.meta_human_path,
        storage_root=args.storage_root,
        mood=args.mood,
        mood_intensity=args.mood_intensity,
        process_mask=args.process_mask,
        head_movement_mode=args.head_movement_mode,
    )


if __name__ == "__main__":
    main()
