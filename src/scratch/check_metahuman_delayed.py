import unreal


HANDLE = None
TARGET_SECONDS = 5.0
ELAPSED_SECONDS = 0.0


def log_state(tag: str) -> None:
    unreal.log(f"=== CHECK_METAHUMAN_DELAYED {tag} ===")
    unreal.log(f"HAS MetaHumanPerformance: {hasattr(unreal, 'MetaHumanPerformance')}")
    unreal.log(f"HAS MetaHumanPerformanceFactoryNew: {hasattr(unreal, 'MetaHumanPerformanceFactoryNew')}")
    unreal.log(f"HAS MetaHumanPerformanceExportUtils: {hasattr(unreal, 'MetaHumanPerformanceExportUtils')}")


def on_tick(delta_seconds: float) -> None:
    global HANDLE, ELAPSED_SECONDS

    ELAPSED_SECONDS += delta_seconds
    if ELAPSED_SECONDS < TARGET_SECONDS:
        return

    log_state("AFTER_WAIT")
    if HANDLE is not None:
        unreal.unregister_slate_post_tick_callback(HANDLE)
    unreal.EditorPythonScriptingLibrary.set_keep_python_script_alive(False)
    unreal.SystemLibrary.quit_editor()


log_state("IMMEDIATE")
unreal.EditorPythonScriptingLibrary.set_keep_python_script_alive(True)
HANDLE = unreal.register_slate_post_tick_callback(on_tick)
