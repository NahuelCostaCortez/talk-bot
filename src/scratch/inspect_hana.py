import unreal


def dump_component(obj, prefix=""):
    if not obj:
        return
    name = obj.get_name()
    cls = obj.get_class().get_name()
    unreal.log(f"{prefix}COMPONENT {name} :: {cls}")
    for prop in obj.get_class().get_properties():
        prop_name = prop.get_name()
        prop_name_lower = prop_name.lower()
        if any(token in prop_name_lower for token in ("live", "link", "subject", "perform", "audio", "face")):
            try:
                value = obj.get_editor_property(prop_name)
            except Exception as exc:
                value = f"<unreadable:{exc}>"
            unreal.log(f"{prefix}  PROP {prop_name} = {value}")


asset = unreal.load_asset("/Game/MetaHumans/Hana/BP_Hana")
if not asset:
    raise RuntimeError("Could not load /Game/MetaHumans/Hana/BP_Hana")

unreal.log(f"ASSET {asset.get_name()} :: {asset.get_class().get_name()}")

generated_class = asset.generated_class()
unreal.log(f"GENERATED_CLASS {generated_class.get_name()}")

cdo = unreal.get_default_object(generated_class)
unreal.log(f"CDO {cdo.get_name()} :: {cdo.get_class().get_name()}")

for comp in cdo.get_components_by_class(unreal.ActorComponent):
    dump_component(comp)

for prop in cdo.get_class().get_properties():
    prop_name = prop.get_name()
    prop_name_lower = prop_name.lower()
    if any(token in prop_name_lower for token in ("live", "link", "subject", "perform", "audio", "face")):
        try:
            value = cdo.get_editor_property(prop_name)
        except Exception as exc:
            value = f"<unreadable:{exc}>"
        unreal.log(f"ACTOR_PROP {prop_name} = {value}")
