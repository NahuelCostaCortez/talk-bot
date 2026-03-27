import unreal

prefixes = (
    "MetaHuman",
    "AudioDrivenAnimation",
    "Performance",
)

matches = sorted(name for name in dir(unreal) if any(name.startswith(prefix) for prefix in prefixes))

unreal.log("=== CHECK_METAHUMAN_BINDINGS BEGIN ===")
for name in matches:
    unreal.log(name)
unreal.log(f"HAS MetaHumanPerformance: {hasattr(unreal, 'MetaHumanPerformance')}")
unreal.log(f"HAS MetaHumanPerformanceFactoryNew: {hasattr(unreal, 'MetaHumanPerformanceFactoryNew')}")
unreal.log(f"HAS MetaHumanPerformanceExportUtils: {hasattr(unreal, 'MetaHumanPerformanceExportUtils')}")
unreal.log("=== CHECK_METAHUMAN_BINDINGS END ===")
