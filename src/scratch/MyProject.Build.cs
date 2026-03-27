using UnrealBuildTool;

public class MyProject : ModuleRules
{
    public MyProject(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InputCore",
                "Json",
                "JsonUtilities",
                "WebSockets"
            }
        );

        if (Target.bBuildEditor)
        {
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
}
