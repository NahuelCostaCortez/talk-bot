using UnrealBuildTool;

public class MyProjectEditor : ModuleRules
{
    public MyProjectEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "MyProject"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "AssetTools",
                "LevelSequence",
                "MetaHumanCoreTech",
                "MetaHumanPerformance",
                "MetaHumanSpeech2Face",
                "UnrealEd"
            }
        );
    }
}
