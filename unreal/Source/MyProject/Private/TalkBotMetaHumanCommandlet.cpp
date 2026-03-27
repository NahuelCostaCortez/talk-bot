#include "TalkBotMetaHumanCommandlet.h"

#if WITH_EDITOR

#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AudioDrivenAnimationConfig.h"
#include "AudioDrivenAnimationMood.h"
#include "EditorAnimUtils.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "LevelSequence.h"
#include "MetaHumanPerformance.h"
#include "MetaHumanPerformanceExportUtils.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"

namespace
{
    constexpr TCHAR DefaultMetaHumanPath[] = TEXT("/Game/MetaHumans/Hana/BP_Hana.BP_Hana");
    constexpr TCHAR DefaultStorageRoot[] = TEXT("/Game/TalkBot");
    constexpr TCHAR DefaultBridgeRuntimeDir[] = TEXT("C:/Users/costanahuel/Desktop/talk-bot/bridge/runtime/sessions");
    constexpr TCHAR FaceArchetypeSkeletonPath[] = TEXT("/Game/MetaHumans/Common/Face/Face_Archetype_Skeleton.Face_Archetype_Skeleton");

    FString SanitizeAssetName(const FString& Value)
    {
        FString Result;
        Result.Reserve(Value.Len());

        for (const TCHAR Character : Value)
        {
            Result.AppendChar(FChar::IsAlnum(Character) ? Character : TEXT('_'));
        }

        while (Result.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
        {
        }

        Result.TrimStartAndEndInline();
        Result.TrimCharInline(TEXT('_'), nullptr);

        return Result.IsEmpty() ? TEXT("TalkBotAudio") : Result;
    }

    FString BuildAssetStem(const FString& WavPath)
    {
        const FString SessionName = FPaths::GetBaseFilename(FPaths::GetPath(WavPath));
        const FString WavBaseName = FPaths::GetBaseFilename(WavPath);
        return SanitizeAssetName(FString::Printf(TEXT("TB_%s_%s"), *SessionName, *WavBaseName));
    }

    bool FindLatestWav(const FString& RuntimeSessionsDir, FString& OutWavPath)
    {
        TArray<FString> WavFiles;
        IFileManager::Get().FindFilesRecursive(WavFiles, *RuntimeSessionsDir, TEXT("assistant-turn-*.wav"), true, false);
        if (WavFiles.Num() == 0)
        {
            return false;
        }

        WavFiles.Sort([](const FString& Left, const FString& Right)
        {
            return IFileManager::Get().GetTimeStamp(*Left) > IFileManager::Get().GetTimeStamp(*Right);
        });

        OutWavPath = WavFiles[0];
        return true;
    }

    EAudioDrivenAnimationMood ParseMood(const FString& Mood)
    {
        if (Mood.Equals(TEXT("Neutral"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Neutral;
        }
        if (Mood.Equals(TEXT("Happy"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Happiness;
        }
        if (Mood.Equals(TEXT("Sad"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Sadness;
        }
        if (Mood.Equals(TEXT("Disgust"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Disgust;
        }
        if (Mood.Equals(TEXT("Anger"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Anger;
        }
        if (Mood.Equals(TEXT("Surprise"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Surprise;
        }
        if (Mood.Equals(TEXT("Fear"), ESearchCase::IgnoreCase))
        {
            return EAudioDrivenAnimationMood::Fear;
        }

        return EAudioDrivenAnimationMood::AutoDetect;
    }

    EAudioDrivenAnimationOutputControls ParseProcessMask(const FString& ProcessMask)
    {
        return ProcessMask.Equals(TEXT("MouthOnly"), ESearchCase::IgnoreCase)
            ? EAudioDrivenAnimationOutputControls::MouthOnly
            : EAudioDrivenAnimationOutputControls::FullFace;
    }

    bool EnableHeadMovement(const FString& HeadMovementMode)
    {
        return !HeadMovementMode.Equals(TEXT("Disabled"), ESearchCase::IgnoreCase);
    }

    FString MakeObjectPath(const FString& PackagePath, const FString& AssetName)
    {
        return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
    }

    USoundWave* ImportSoundWave(const FString& WavPath, const FString& DestinationPath, const FString& DestinationName)
    {
        UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
        ImportTask->Filename = WavPath;
        ImportTask->DestinationPath = DestinationPath;
        ImportTask->DestinationName = DestinationName;
        ImportTask->bAutomated = true;
        ImportTask->bSave = true;
        ImportTask->bReplaceExisting = true;
        ImportTask->bReplaceExistingSettings = true;

        FAssetToolsModule::GetModule().Get().ImportAssetTasks({ ImportTask });

        if (ImportTask->ImportedObjectPaths.Num() == 0)
        {
            return nullptr;
        }

        return Cast<USoundWave>(FSoftObjectPath(ImportTask->ImportedObjectPaths[0]).TryLoad());
    }

    FString ComputeUniqueAssetName(const EditorAnimUtils::FNameDuplicationRule& Rule, const UObject* SourceAsset)
    {
        const FString BaseName = Rule.Rename(SourceAsset);
        FString PackageName;
        FString AssetName;
        FAssetToolsModule::GetModule().Get().CreateUniqueAssetName(Rule.FolderPath / BaseName, TEXT(""), PackageName, AssetName);
        return AssetName;
    }

    USkeletalMesh* ResolveVisualizationMesh()
    {
        if (USkeleton* FaceArchetypeSkeleton = LoadObject<USkeleton>(nullptr, FaceArchetypeSkeletonPath))
        {
            return FaceArchetypeSkeleton->GetPreviewMesh(true);
        }

        return nullptr;
    }

    UMetaHumanPerformance* CreatePerformanceAsset(const FString& PackagePath, const FString& AssetName)
    {
        return Cast<UMetaHumanPerformance>(
            FAssetToolsModule::GetModule().Get().CreateAsset(
                AssetName,
                PackagePath,
                UMetaHumanPerformance::StaticClass(),
                nullptr
            )
        );
    }

    void ConfigurePerformance(
        UMetaHumanPerformance* Performance,
        USoundWave* SoundWave,
        EAudioDrivenAnimationMood Mood,
        EAudioDrivenAnimationOutputControls OutputControls,
        bool bEnableHeadMovement
    )
    {
        check(Performance);
        check(SoundWave);

        Performance->InputType = EDataInputType::Audio;
        Performance->Audio = SoundWave;
        Performance->VisualizationMesh = ResolveVisualizationMesh();
        Performance->bGenerateBlinks = true;
        Performance->bDownmixChannels = true;
        Performance->AudioChannelIndex = 0;
        Performance->HeadMovementMode = bEnableHeadMovement
            ? EPerformanceHeadMovementMode::ControlRig
            : EPerformanceHeadMovementMode::Disabled;
        Performance->AudioDrivenAnimationSolveOverrides.Mood = Mood;
        Performance->AudioDrivenAnimationSolveOverrides.MoodIntensity = 1.0f;
        Performance->AudioDrivenAnimationOutputControls = OutputControls;

        static const FName AudioPropertyName = GET_MEMBER_NAME_CHECKED(UMetaHumanPerformance, Audio);
        FProperty* AudioProperty = UMetaHumanPerformance::StaticClass()->FindPropertyByName(AudioPropertyName);
        FPropertyChangedEvent AudioChangedEvent(AudioProperty);
        Performance->PostEditChangeProperty(AudioChangedEvent);
    }

    bool ProcessPerformance(UMetaHumanPerformance* Performance)
    {
        if (!Performance || !Performance->CanProcess())
        {
            return false;
        }

        Performance->SetBlockingProcessing(true);
        const EStartPipelineErrorType StartPipelineError = Performance->StartPipeline(true);
        return StartPipelineError == EStartPipelineErrorType::None && Performance->ContainsAnimationData();
    }

    ULevelSequence* ExportLevelSequence(
        UMetaHumanPerformance* Performance,
        const FString& PackagePath,
        const FString& AssetName,
        const FString& MetaHumanPath,
        bool bEnableHeadMovement
    )
    {
        if (!Performance || !Performance->CanExportAnimation())
        {
            return nullptr;
        }

        UMetaHumanPerformanceExportLevelSequenceSettings* ExportSettings = GetMutableDefault<UMetaHumanPerformanceExportLevelSequenceSettings>();
        ExportSettings->bShowExportDialog = false;
        ExportSettings->bExportVideoTrack = false;
        ExportSettings->bExportDepthTrack = false;
        ExportSettings->bExportDepthMesh = false;
        ExportSettings->bExportAudioTrack = true;
        ExportSettings->bExportCamera = true;
        ExportSettings->bExportImagePlane = false;
        ExportSettings->bExportIdentity = false;
        ExportSettings->bExportControlRigTrack = false;
        ExportSettings->bEnableControlRigHeadMovement = false;
        ExportSettings->bExportTransformTrack = false;
        ExportSettings->bEnableMetaHumanHeadMovement = bEnableHeadMovement;
        ExportSettings->PackagePath = PackagePath;
        ExportSettings->AssetName = AssetName;
        ExportSettings->TargetMetaHumanClass = LoadObject<UBlueprint>(nullptr, *MetaHumanPath);

        return UMetaHumanPerformanceExportUtils::ExportLevelSequence(Performance, ExportSettings);
    }
}

UTalkBotMetaHumanCommandlet::UTalkBotMetaHumanCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UTalkBotMetaHumanCommandlet::Main(const FString& Params)
{
    FString WavPath;
    FString RuntimeSessionsDir = DefaultBridgeRuntimeDir;
    FString MetaHumanPath = DefaultMetaHumanPath;
    FString StorageRoot = DefaultStorageRoot;
    FString Mood = TEXT("Auto");
    FString ProcessMask = TEXT("FullFace");
    FString HeadMovementMode = TEXT("ControlRig");

    const bool bLatest = FParse::Param(*Params, TEXT("Latest"));
    FParse::Value(*Params, TEXT("WavPath="), WavPath);
    FParse::Value(*Params, TEXT("BridgeRuntimeDir="), RuntimeSessionsDir);
    FParse::Value(*Params, TEXT("MetaHumanPath="), MetaHumanPath);
    FParse::Value(*Params, TEXT("StorageRoot="), StorageRoot);
    FParse::Value(*Params, TEXT("Mood="), Mood);
    FParse::Value(*Params, TEXT("ProcessMask="), ProcessMask);
    FParse::Value(*Params, TEXT("HeadMovementMode="), HeadMovementMode);

    if (WavPath.IsEmpty())
    {
        if (!bLatest || !FindLatestWav(RuntimeSessionsDir, WavPath))
        {
            UE_LOG(LogTemp, Error, TEXT("No WAV available. Pass -WavPath=<absolute path> or use -Latest with a valid bridge runtime directory."));
            return 1;
        }
    }

    WavPath = FPaths::ConvertRelativePathToFull(WavPath);
    if (!FPaths::FileExists(WavPath))
    {
        UE_LOG(LogTemp, Error, TEXT("WAV path does not exist: %s"), *WavPath);
        return 1;
    }

    const FString AudioPath = StorageRoot / TEXT("Audio");
    const FString PerformancePath = StorageRoot / TEXT("Performance");
    const FString SequencePath = StorageRoot / TEXT("Sequences");
    const FString AssetStem = BuildAssetStem(WavPath);

    USoundWave* ImportedSoundWave = ImportSoundWave(WavPath, AudioPath, AssetStem);
    if (!ImportedSoundWave)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to import WAV as SoundWave: %s"), *WavPath);
        return 1;
    }

    EditorAnimUtils::FNameDuplicationRule PerformanceRule;
    PerformanceRule.FolderPath = PerformancePath;
    PerformanceRule.Prefix = TEXT("PERF_");

    EditorAnimUtils::FNameDuplicationRule ExportRule;
    ExportRule.FolderPath = SequencePath;
    ExportRule.Prefix = TEXT("LS_");

    const FString PerformanceAssetName = ComputeUniqueAssetName(PerformanceRule, ImportedSoundWave);
    const FString LevelSequenceAssetName = ComputeUniqueAssetName(ExportRule, ImportedSoundWave);

    UMetaHumanPerformance* Performance = CreatePerformanceAsset(PerformancePath, PerformanceAssetName);
    if (!Performance)
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create MetaHuman Performance asset in %s"), *PerformancePath);
        return 1;
    }

    const bool bEnableHeadMovement = EnableHeadMovement(HeadMovementMode);
    ConfigurePerformance(
        Performance,
        ImportedSoundWave,
        ParseMood(Mood),
        ParseProcessMask(ProcessMask),
        bEnableHeadMovement
    );

    if (!ProcessPerformance(Performance))
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to process MetaHuman Performance asset: %s"), *Performance->GetPathName());
        return 1;
    }

    ULevelSequence* LevelSequence = ExportLevelSequence(
        Performance,
        SequencePath,
        LevelSequenceAssetName,
        MetaHumanPath,
        bEnableHeadMovement
    );

    UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);

    if (!LevelSequence)
    {
        UE_LOG(LogTemp, Error, TEXT("MetaHuman processing completed but level-sequence export failed."));
        UE_LOG(LogTemp, Warning, TEXT("Expected Level Sequence path: %s"), *MakeObjectPath(SequencePath, LevelSequenceAssetName));
        return 1;
    }

    UE_LOG(LogTemp, Display, TEXT("TalkBot MetaHuman processing complete"));
    UE_LOG(LogTemp, Display, TEXT("WAV source: %s"), *WavPath);
    UE_LOG(LogTemp, Display, TEXT("SoundWave: %s"), *ImportedSoundWave->GetPathName());
    UE_LOG(LogTemp, Display, TEXT("Performance: %s"), *Performance->GetPathName());
    UE_LOG(LogTemp, Display, TEXT("Level Sequence: %s"), *LevelSequence->GetPathName());

    return 0;
}

#else

UTalkBotMetaHumanCommandlet::UTalkBotMetaHumanCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
    ShowErrorCount = true;
}

int32 UTalkBotMetaHumanCommandlet::Main(const FString& Params)
{
    UE_LOG(LogTemp, Error, TEXT("TalkBotMetaHumanCommandlet requires an editor build."));
    return 1;
}

#endif
