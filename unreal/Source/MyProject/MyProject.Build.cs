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
                "LevelSequence",
                "LiveLink",
                "LiveLinkInterface",
                "MetaHumanCoreTech",
                "MetaHumanLiveLinkSource",
                "MetaHumanLocalLiveLinkSource",
                "MetaHumanPipelineCore",
                "MovieScene",
                "MovieSceneTracks",
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
                    "MetaHumanPerformance",
                    "MetaHumanSpeech2Face",
                    "UnrealEd"
                }
            );
        }
    }
}
