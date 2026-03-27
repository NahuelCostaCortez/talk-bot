#include "TalkBotBridgeActor.h"

#include "Async/Async.h"
#include "Components/ActorComponent.h"
#include "Components/AudioComponent.h"
#include "Components/LODSyncComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Blueprint.h"
#include "HAL/PlatformProcess.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"
#include "IWebSocket.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MetaHumanAudioBaseLiveLinkSubject.h"
#include "MetaHumanAudioBaseLiveLinkSubjectSettings.h"
#include "MetaHumanAudioLiveLinkSourceSettings.h"
#include "MetaHumanLocalLiveLinkSource.h"
#include "MetaHumanLocalLiveLinkSourceSettings.h"
#include "Misc/Base64.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSequenceID.h"
#include "MovieSceneSpawnable.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/UnrealType.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "WebSocketsModule.h"

#include "AudioDrivenAnimationMood.h"

#if WITH_EDITOR
#include "Animation/Skeleton.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "AudioDrivenAnimationConfig.h"
#include "EditorAnimUtils.h"
#include "FileHelpers.h"
#include "MetaHumanPerformance.h"
#include "MetaHumanPerformanceExportUtils.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogTalkBotBridge, Log, All);

namespace
{
    constexpr TCHAR FaceArchetypeSkeletonPath[] = TEXT("/Game/MetaHumans/Common/Face/Face_Archetype_Skeleton.Face_Archetype_Skeleton");
    constexpr int32 RealtimeTranscriptWindowChars = 180;

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

    FString BuildObjectPath(const FString& PackagePath, const FString& AssetName)
    {
        return FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
    }

    FString BuildPreferredLevelSequencePath(const FString& StorageRoot, const FString& WavPath)
    {
        const FString AssetStem = BuildAssetStem(WavPath);
        const FString SequencePath = StorageRoot / TEXT("Sequences");
        const FString AssetName = FString::Printf(TEXT("LS_%s_Hana"), *AssetStem);
        return BuildObjectPath(SequencePath, AssetName);
    }

    FString BuildPreferredSoundWavePath(const FString& StorageRoot, const FString& WavPath)
    {
        const FString AssetStem = BuildAssetStem(WavPath);
        const FString AudioPath = StorageRoot / TEXT("Audio");
        return BuildObjectPath(AudioPath, AssetStem);
    }

    FString BuildLegacyLevelSequencePath(const FString& StorageRoot, const FString& WavPath)
    {
        const FString AssetStem = BuildAssetStem(WavPath);
        const FString SequencePath = StorageRoot / TEXT("Sequences");
        const FString AssetName = FString::Printf(TEXT("LS_%s"), *AssetStem);
        return BuildObjectPath(SequencePath, AssetName);
    }

    bool BindingLooksLikeHana(UMovieScene* MovieScene, const FGuid& BindingGuid)
    {
        if (!MovieScene)
        {
            return false;
        }

        auto MatchesHana = [](const FString& Value)
        {
            return Value.Contains(TEXT("Hana"), ESearchCase::IgnoreCase)
                || Value.Contains(TEXT("BP_Hana"), ESearchCase::IgnoreCase)
                || Value.Contains(TEXT("BP Hana"), ESearchCase::IgnoreCase);
        };

        if (MatchesHana(MovieScene->GetObjectDisplayName(BindingGuid).ToString()))
        {
            return true;
        }

        if (const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(BindingGuid))
        {
            if (MatchesHana(Possessable->GetName()))
            {
                return true;
            }
        }

        if (const FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(BindingGuid))
        {
            if (MatchesHana(Spawnable->GetName()))
            {
                return true;
            }

            if (const UObject* ObjectTemplate = Spawnable->GetObjectTemplate())
            {
                if (MatchesHana(ObjectTemplate->GetName()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool EnsureMetaHumanBindingTag(ULevelSequence* LevelSequence, const FName BindingTag)
    {
        if (!LevelSequence || BindingTag.IsNone())
        {
            return false;
        }

        if (LevelSequence->FindBindingsByTag(BindingTag).Num() > 0)
        {
            return true;
        }

        UMovieScene* MovieScene = LevelSequence->GetMovieScene();
        if (!MovieScene)
        {
            return false;
        }

        const UMovieScene* ConstMovieScene = MovieScene;
        for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
        {
            const FGuid BindingGuid = Binding.GetObjectGuid();
            if (!BindingLooksLikeHana(MovieScene, BindingGuid))
            {
                continue;
            }

            MovieScene->AddNewBindingTag(BindingTag);
            MovieScene->TagBinding(
                BindingTag,
                UE::MovieScene::FFixedObjectBindingID(BindingGuid, MovieSceneSequenceID::Root)
            );
            LevelSequence->MarkPackageDirty();
            return true;
        }

        return false;
    }

    void SanitizeLevelSequenceForRuntimePlayback(ULevelSequence* LevelSequence)
    {
        if (!LevelSequence)
        {
            return;
        }

        UMovieScene* MovieScene = LevelSequence->GetMovieScene();
        if (!MovieScene)
        {
            return;
        }

        if (MovieScene->GetCameraCutTrack())
        {
            MovieScene->RemoveCameraCutTrack();
        }

        TArray<UMovieSceneTrack*> TracksToRemove;
        for (UMovieSceneTrack* Track : MovieScene->GetTracks())
        {
            if (Track && (Track->IsA<UMovieSceneAudioTrack>() || Track->IsA<UMovieSceneFadeTrack>()))
            {
                TracksToRemove.Add(Track);
            }
        }

        for (UMovieSceneTrack* Track : TracksToRemove)
        {
            MovieScene->RemoveTrack(*Track);
        }
    }

    EAudioDrivenAnimationMood ParseRealtimeMood(const ETalkBotLipsyncMood Mood)
    {
        if (Mood == ETalkBotLipsyncMood::AutoDetect)
        {
            // Epic's realtime solver indexes directly into a fixed mood array and cannot handle AutoDetect=255.
            return EAudioDrivenAnimationMood::Neutral;
        }
        if (Mood == ETalkBotLipsyncMood::Neutral)
        {
            return EAudioDrivenAnimationMood::Neutral;
        }
        if (Mood == ETalkBotLipsyncMood::Happy)
        {
            return EAudioDrivenAnimationMood::Happiness;
        }
        if (Mood == ETalkBotLipsyncMood::Sad)
        {
            return EAudioDrivenAnimationMood::Sadness;
        }
        if (Mood == ETalkBotLipsyncMood::Disgust)
        {
            return EAudioDrivenAnimationMood::Disgust;
        }
        if (Mood == ETalkBotLipsyncMood::Anger)
        {
            return EAudioDrivenAnimationMood::Anger;
        }
        if (Mood == ETalkBotLipsyncMood::Surprise)
        {
            return EAudioDrivenAnimationMood::Surprise;
        }
        if (Mood == ETalkBotLipsyncMood::Fear)
        {
            return EAudioDrivenAnimationMood::Fear;
        }

        return EAudioDrivenAnimationMood::AutoDetect;
    }

    float ComputePcmEnergy(const TArray<uint8>& DecodedPcm)
    {
        const int32 SampleCount = DecodedPcm.Num() / static_cast<int32>(sizeof(int16));
        if (SampleCount <= 0)
        {
            return 0.0f;
        }

        const int16* Samples = reinterpret_cast<const int16*>(DecodedPcm.GetData());
        double SumSquares = 0.0;
        for (int32 Index = 0; Index < SampleCount; ++Index)
        {
            const double Normalized = static_cast<double>(Samples[Index]) / 32768.0;
            SumSquares += Normalized * Normalized;
        }

        const double Rms = FMath::Sqrt(SumSquares / static_cast<double>(SampleCount));
        return FMath::Clamp(static_cast<float>(Rms * 3.5), 0.0f, 1.0f);
    }

    int32 CountMoodHits(const FString& Text, const TArray<const TCHAR*>& Keywords)
    {
        int32 Hits = 0;
        for (const TCHAR* Keyword : Keywords)
        {
            if (Text.Contains(Keyword))
            {
                ++Hits;
            }
        }

        return Hits;
    }

    ETalkBotLipsyncMood InferMoodFromTranscript(const FString& Transcript)
    {
        if (Transcript.IsEmpty())
        {
            return ETalkBotLipsyncMood::Neutral;
        }

        const FString Window = Transcript.Right(RealtimeTranscriptWindowChars).ToLower();

        static const TArray<const TCHAR*> HappyKeywords = {
            TEXT("genial"), TEXT("perfect"), TEXT("estupend"), TEXT("encanta"), TEXT("me alegra"),
            TEXT("buen"), TEXT("great"), TEXT("awesome"), TEXT("fantastic"), TEXT("glad")
        };
        static const TArray<const TCHAR*> SadKeywords = {
            TEXT("lo siento"), TEXT("lamento"), TEXT("pena"), TEXT("triste"), TEXT("dificil"),
            TEXT("complicad"), TEXT("sorry"), TEXT("unfortunately")
        };
        static const TArray<const TCHAR*> SurpriseKeywords = {
            TEXT("wow"), TEXT("vaya"), TEXT("anda"), TEXT("incre"), TEXT("sorpr"), TEXT("!"),
            TEXT("que bien")
        };
        static const TArray<const TCHAR*> FearKeywords = {
            TEXT("cuidado"), TEXT("ojo"), TEXT("riesgo"), TEXT("peligro"), TEXT("warning")
        };
        static const TArray<const TCHAR*> AngerKeywords = {
            TEXT("grave"), TEXT("fatal"), TEXT("inadmis"), TEXT("frustr"), TEXT("serio")
        };

        int32 BestScore = 0;
        ETalkBotLipsyncMood BestMood = ETalkBotLipsyncMood::Neutral;

        const auto ConsiderMood = [&](const ETalkBotLipsyncMood Mood, const int32 Score)
        {
            if (Score > BestScore)
            {
                BestScore = Score;
                BestMood = Mood;
            }
        };

        ConsiderMood(ETalkBotLipsyncMood::Happy, CountMoodHits(Window, HappyKeywords));
        ConsiderMood(ETalkBotLipsyncMood::Sad, CountMoodHits(Window, SadKeywords));
        ConsiderMood(ETalkBotLipsyncMood::Surprise, CountMoodHits(Window, SurpriseKeywords));
        ConsiderMood(ETalkBotLipsyncMood::Fear, CountMoodHits(Window, FearKeywords));
        ConsiderMood(ETalkBotLipsyncMood::Anger, CountMoodHits(Window, AngerKeywords));

        return BestMood;
    }

    float ComputeTranscriptMoodBoost(const FString& Transcript)
    {
        if (Transcript.IsEmpty())
        {
            return 0.0f;
        }

        const FString Window = Transcript.Right(RealtimeTranscriptWindowChars);
        float Boost = 0.0f;

        if (Window.Contains(TEXT("!")))
        {
            Boost += 0.12f;
        }
        if (Window.Contains(TEXT("?")))
        {
            Boost += 0.05f;
        }

        const FString Lower = Window.ToLower();
        if (Lower.Contains(TEXT("muy")) || Lower.Contains(TEXT("super")) || Lower.Contains(TEXT("realmente")) || Lower.Contains(TEXT("very")))
        {
            Boost += 0.08f;
        }

        return FMath::Clamp(Boost, 0.0f, 0.25f);
    }

    FString ExtractTranscriptTextFromPayload(const TSharedPtr<FJsonObject>& Payload)
    {
        if (!Payload.IsValid())
        {
            return FString();
        }

        FString Transcript;
        if (Payload->TryGetStringField(TEXT("delta"), Transcript) && !Transcript.IsEmpty())
        {
            return Transcript;
        }

        if (Payload->TryGetStringField(TEXT("transcript"), Transcript) && !Transcript.IsEmpty())
        {
            return Transcript;
        }

        if (Payload->TryGetStringField(TEXT("text"), Transcript) && !Transcript.IsEmpty())
        {
            return Transcript;
        }

        return FString();
    }

    bool SetBoolPropertyByName(UObject* Object, const FName PropertyName, const bool bValue)
    {
        if (!Object)
        {
            return false;
        }

        if (FBoolProperty* BoolProperty = FindFProperty<FBoolProperty>(Object->GetClass(), PropertyName))
        {
            BoolProperty->SetPropertyValue_InContainer(Object, bValue);
            return true;
        }

        return false;
    }

    bool SetNameOrStringPropertyByName(UObject* Object, const FName PropertyName, const FName NameValue, const FString& StringValue)
    {
        if (!Object)
        {
            return false;
        }

        if (FNameProperty* NameProperty = FindFProperty<FNameProperty>(Object->GetClass(), PropertyName))
        {
            NameProperty->SetPropertyValue_InContainer(Object, NameValue);
            return true;
        }

        if (FStrProperty* StrProperty = FindFProperty<FStrProperty>(Object->GetClass(), PropertyName))
        {
            StrProperty->SetPropertyValue_InContainer(Object, StringValue);
            return true;
        }

        if (FStructProperty* StructProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName))
        {
            if (StructProperty->Struct == FLiveLinkSubjectName::StaticStruct())
            {
                if (FLiveLinkSubjectName* SubjectName = StructProperty->ContainerPtrToValuePtr<FLiveLinkSubjectName>(Object))
                {
                    *SubjectName = FLiveLinkSubjectName(NameValue);
                    return true;
                }
            }
        }

        return false;
    }

    bool CallNoArgFunction(UObject* Object, const FName FunctionName)
    {
        if (!Object)
        {
            return false;
        }

        if (UFunction* Function = Object->FindFunction(FunctionName))
        {
            if (Function->ParmsSize == 0)
            {
                Object->ProcessEvent(Function, nullptr);
                return true;
            }
        }

        return false;
    }

    bool CallSingleNameOrStringFunction(UObject* Object, const FName FunctionName, const FName NameValue, const FString& StringValue)
    {
        if (!Object)
        {
            return false;
        }

        UFunction* Function = Object->FindFunction(FunctionName);
        if (!Function)
        {
            return false;
        }

        FProperty* ParamProperty = nullptr;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ParamProperty = *It;
                break;
            }
        }

        if (!ParamProperty)
        {
            return false;
        }

        TArray<uint8> ParameterBuffer;
        ParameterBuffer.SetNumZeroed(Function->ParmsSize);
        ParamProperty->InitializeValue_InContainer(ParameterBuffer.GetData());

        bool bInvoked = false;
        if (FNameProperty* NameProperty = CastField<FNameProperty>(ParamProperty))
        {
            NameProperty->SetPropertyValue_InContainer(ParameterBuffer.GetData(), NameValue);
            bInvoked = true;
        }
        else if (FStrProperty* StrProperty = CastField<FStrProperty>(ParamProperty))
        {
            StrProperty->SetPropertyValue_InContainer(ParameterBuffer.GetData(), StringValue);
            bInvoked = true;
        }
        else if (FStructProperty* StructProperty = CastField<FStructProperty>(ParamProperty))
        {
            if (StructProperty->Struct == FLiveLinkSubjectName::StaticStruct())
            {
                if (FLiveLinkSubjectName* SubjectName = StructProperty->ContainerPtrToValuePtr<FLiveLinkSubjectName>(ParameterBuffer.GetData()))
                {
                    *SubjectName = FLiveLinkSubjectName(NameValue);
                    bInvoked = true;
                }
            }
        }

        if (bInvoked)
        {
            Object->ProcessEvent(Function, ParameterBuffer.GetData());
        }

        ParamProperty->DestroyValue_InContainer(ParameterBuffer.GetData());
        return bInvoked;
    }

    bool CallSingleBoolFunction(UObject* Object, const FName FunctionName, const bool bValue)
    {
        if (!Object)
        {
            return false;
        }

        UFunction* Function = Object->FindFunction(FunctionName);
        if (!Function)
        {
            return false;
        }

        FProperty* ParamProperty = nullptr;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ParamProperty = *It;
                break;
            }
        }

        if (!ParamProperty)
        {
            return false;
        }

        TArray<uint8> ParameterBuffer;
        ParameterBuffer.SetNumZeroed(Function->ParmsSize);
        ParamProperty->InitializeValue_InContainer(ParameterBuffer.GetData());

        bool bInvoked = false;
        if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(ParamProperty))
        {
            BoolProperty->SetPropertyValue_InContainer(ParameterBuffer.GetData(), bValue);
            bInvoked = true;
        }

        if (bInvoked)
        {
            Object->ProcessEvent(Function, ParameterBuffer.GetData());
        }

        ParamProperty->DestroyValue_InContainer(ParameterBuffer.GetData());
        return bInvoked;
    }

#if WITH_EDITOR
    EAudioDrivenAnimationMood ParseMood(const ETalkBotLipsyncMood Mood)
    {
        if (Mood == ETalkBotLipsyncMood::Neutral)
        {
            return EAudioDrivenAnimationMood::Neutral;
        }
        if (Mood == ETalkBotLipsyncMood::Happy)
        {
            return EAudioDrivenAnimationMood::Happiness;
        }
        if (Mood == ETalkBotLipsyncMood::Sad)
        {
            return EAudioDrivenAnimationMood::Sadness;
        }
        if (Mood == ETalkBotLipsyncMood::Disgust)
        {
            return EAudioDrivenAnimationMood::Disgust;
        }
        if (Mood == ETalkBotLipsyncMood::Anger)
        {
            return EAudioDrivenAnimationMood::Anger;
        }
        if (Mood == ETalkBotLipsyncMood::Surprise)
        {
            return EAudioDrivenAnimationMood::Surprise;
        }
        if (Mood == ETalkBotLipsyncMood::Fear)
        {
            return EAudioDrivenAnimationMood::Fear;
        }

        return EAudioDrivenAnimationMood::AutoDetect;
    }

    EAudioDrivenAnimationOutputControls ParseProcessMask(const ETalkBotLipsyncProcessMask ProcessMask)
    {
        return ProcessMask == ETalkBotLipsyncProcessMask::MouthOnly
            ? EAudioDrivenAnimationOutputControls::MouthOnly
            : EAudioDrivenAnimationOutputControls::FullFace;
    }

    bool EnableHeadMovement(const ETalkBotLipsyncHeadMovement HeadMovementMode)
    {
        return HeadMovementMode != ETalkBotLipsyncHeadMovement::Disabled;
    }

    USoundWave* ImportSoundWave(
        const FString& WavPath,
        const FString& DestinationPath,
        const FString& DestinationName,
        const bool bSaveAsset
    )
    {
        UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
        ImportTask->Filename = WavPath;
        ImportTask->DestinationPath = DestinationPath;
        ImportTask->DestinationName = DestinationName;
        ImportTask->bAutomated = true;
        ImportTask->bSave = bSaveAsset;
        ImportTask->bReplaceExisting = true;
        ImportTask->bReplaceExistingSettings = true;

        FAssetToolsModule::GetModule().Get().ImportAssetTasks({ ImportTask });

        if (ImportTask->ImportedObjectPaths.Num() == 0)
        {
            return nullptr;
        }

        return Cast<USoundWave>(FSoftObjectPath(ImportTask->ImportedObjectPaths[0]).TryLoad());
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
        if (UMetaHumanPerformance* ExistingAsset = LoadObject<UMetaHumanPerformance>(nullptr, *BuildObjectPath(PackagePath, AssetName)))
        {
            return ExistingAsset;
        }

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
        ExportSettings->bExportCamera = false;
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

    ULevelSequence* GenerateLevelSequenceFromWav(
        const FString& WavPath,
        const FString& MetaHumanPath,
        const FString& StorageRoot,
        const FName BindingTag,
        const ETalkBotLipsyncMood Mood,
        const ETalkBotLipsyncProcessMask ProcessMask,
        const ETalkBotLipsyncHeadMovement HeadMovementMode,
        const bool bPersistGeneratedAssets,
        FString& OutError
    )
    {
        const FString NormalizedWavPath = FPaths::ConvertRelativePathToFull(WavPath);
        if (!FPaths::FileExists(NormalizedWavPath))
        {
            OutError = FString::Printf(TEXT("WAV path does not exist: %s"), *NormalizedWavPath);
            return nullptr;
        }

        const FString PreferredSequencePath = BuildPreferredLevelSequencePath(StorageRoot, NormalizedWavPath);
        if (ULevelSequence* ExistingLevelSequence = LoadObject<ULevelSequence>(nullptr, *PreferredSequencePath))
        {
            EnsureMetaHumanBindingTag(ExistingLevelSequence, BindingTag);
            return ExistingLevelSequence;
        }

        const FString AssetStem = BuildAssetStem(NormalizedWavPath);
        const FString AudioPath = StorageRoot / TEXT("Audio");
        const FString PerformancePath = StorageRoot / TEXT("Performance");
        const FString SequencePath = StorageRoot / TEXT("Sequences");
        const FString PerformanceAssetName = FString::Printf(TEXT("PERF_%s"), *AssetStem);
        const FString LevelSequenceAssetName = FString::Printf(TEXT("LS_%s_Hana"), *AssetStem);

        USoundWave* ImportedSoundWave = ImportSoundWave(
            NormalizedWavPath,
            AudioPath,
            AssetStem,
            bPersistGeneratedAssets
        );
        if (!ImportedSoundWave)
        {
            OutError = FString::Printf(TEXT("Failed to import WAV as SoundWave: %s"), *NormalizedWavPath);
            return nullptr;
        }

        UMetaHumanPerformance* Performance = CreatePerformanceAsset(PerformancePath, PerformanceAssetName);
        if (!Performance)
        {
            OutError = FString::Printf(TEXT("Failed to create MetaHuman Performance asset: %s"), *PerformanceAssetName);
            return nullptr;
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
            OutError = FString::Printf(TEXT("Failed to process MetaHuman Performance asset: %s"), *Performance->GetPathName());
            return nullptr;
        }

        ULevelSequence* LevelSequence = ExportLevelSequence(
            Performance,
            SequencePath,
            LevelSequenceAssetName,
            MetaHumanPath,
            bEnableHeadMovement
        );

        if (!LevelSequence)
        {
            OutError = FString::Printf(TEXT("Failed to export Level Sequence asset: %s"), *LevelSequenceAssetName);
            return nullptr;
        }

        EnsureMetaHumanBindingTag(LevelSequence, BindingTag);

        if (bPersistGeneratedAssets)
        {
            UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, true);
        }

        return LevelSequence;
    }
#endif
}

class FTalkBotRealtimeAudioLiveLinkSubject : public FMetaHumanAudioBaseLiveLinkSubject
{
public:
    static constexpr int32 TimestampFrameRate = 50;
    static constexpr double FrameDurationSeconds = 1.0 / 50.0;

    FTalkBotRealtimeAudioLiveLinkSubject(
        ILiveLinkClient* InLiveLinkClient,
        const FGuid& InSourceGuid,
        const FName& InSubjectName,
        UMetaHumanAudioBaseLiveLinkSubjectSettings* InSettings)
        : FMetaHumanAudioBaseLiveLinkSubject(InLiveLinkClient, InSourceGuid, InSubjectName, InSettings)
    {
    }

    void StartStream()
    {
        FScopeLock Lock(&PendingAudioMutex);
        PendingPcm16.Reset();
        PendingSampleOffset = 0;
        PendingSampleRate = 0;
        FrameCounter = 0;
        bFlushRemainder = false;
        bStreamActive = true;
        bHasPlaybackClock = false;
    }

    void FinishStream()
    {
        FScopeLock Lock(&PendingAudioMutex);
        bFlushRemainder = true;
    }

    void PushPcm16(const TArray<uint8>& PcmBytes, const int32 SampleRate)
    {
        const int32 SafeSampleRate = SampleRate > 0 ? SampleRate : 16000;
        const int32 NumSamples = PcmBytes.Num() / static_cast<int32>(sizeof(int16));
        if (NumSamples <= 0)
        {
            return;
        }

        FScopeLock Lock(&PendingAudioMutex);
        if (PendingSampleRate != 0 && PendingSampleRate != SafeSampleRate)
        {
            PendingPcm16.Reset();
            PendingSampleOffset = 0;
            FrameCounter = 0;
            bHasPlaybackClock = false;
        }

        PendingSampleRate = SafeSampleRate;
        PendingPcm16.Append(reinterpret_cast<const int16*>(PcmBytes.GetData()), NumSamples);
        bStreamActive = true;
        bFlushRemainder = false;
    }

protected:
    virtual void MediaSamplerMain() override
    {
        FFrameRate FrameRate(TimestampFrameRate, 1);

        while (IsRunning())
        {
            TArray<int16> ChunkSamples;
            int32 ChunkSampleRate = 0;
            bool bShouldEmitChunk = false;

            {
                FScopeLock Lock(&PendingAudioMutex);

                if (PendingSampleRate > 0)
                {
                    const int32 SamplesPerFrame = FMath::Max(1, FMath::RoundToInt(static_cast<float>(PendingSampleRate) * 0.02f));
                    const int32 AvailableSamples = PendingPcm16.Num() - PendingSampleOffset;

                    if (AvailableSamples >= SamplesPerFrame)
                    {
                        ChunkSamples.Append(PendingPcm16.GetData() + PendingSampleOffset, SamplesPerFrame);
                        PendingSampleOffset += SamplesPerFrame;
                        ChunkSampleRate = PendingSampleRate;
                        bShouldEmitChunk = true;
                    }
                    else if (bFlushRemainder && AvailableSamples > 0)
                    {
                        ChunkSamples.Append(PendingPcm16.GetData() + PendingSampleOffset, AvailableSamples);
                        ChunkSamples.AddZeroed(SamplesPerFrame - AvailableSamples);
                        PendingSampleOffset = PendingPcm16.Num();
                        ChunkSampleRate = PendingSampleRate;
                        bShouldEmitChunk = true;
                        bFlushRemainder = false;
                        bStreamActive = false;
                    }
                    else if (bFlushRemainder && AvailableSamples <= 0)
                    {
                        bFlushRemainder = false;
                        bStreamActive = false;
                    }

                    if (PendingSampleOffset > 0 && PendingSampleOffset >= PendingPcm16.Num())
                    {
                        PendingPcm16.Reset();
                        PendingSampleOffset = 0;
                    }
                    else if (PendingSampleOffset > 4096)
                    {
                        PendingPcm16.RemoveAt(0, PendingSampleOffset, EAllowShrinking::No);
                        PendingSampleOffset = 0;
                    }
                }
            }

            if (!bShouldEmitChunk)
            {
                FPlatformProcess::Sleep(0.002f);
                continue;
            }

            const double Now = FPlatformTime::Seconds();
            if (!bHasPlaybackClock)
            {
                NextPlaybackTimeSeconds = Now;
                bHasPlaybackClock = true;
            }
            else if (NextPlaybackTimeSeconds > Now)
            {
                FPlatformProcess::Sleep(static_cast<float>(NextPlaybackTimeSeconds - Now));
            }
            else if (Now - NextPlaybackTimeSeconds > 0.25)
            {
                NextPlaybackTimeSeconds = Now;
            }

            FAudioSample AudioSample;
            AudioSample.NumChannels = 1;
            AudioSample.SampleRate = ChunkSampleRate;
            AudioSample.NumSamples = ChunkSamples.Num();
            AudioSample.Data.Reserve(ChunkSamples.Num());

            for (const int16 SampleValue : ChunkSamples)
            {
                const float NormalizedSample = static_cast<float>(SampleValue) / 32768.0f;
                AudioSample.Data.Add(FMath::Clamp(NormalizedSample, -1.0f, 1.0f));
            }

            AudioSample.Time = FQualifiedFrameTime(FFrameTime(static_cast<int32>(FrameCounter++)), FrameRate);
            AudioSample.TimeSource = ETimeSource::System;
            AudioSample.NumDropped = 0;
            AddAudioSample(MoveTemp(AudioSample));

            NextPlaybackTimeSeconds += FrameDurationSeconds;
        }
    }

private:
    FCriticalSection PendingAudioMutex;
    TArray<int16> PendingPcm16;
    int32 PendingSampleOffset = 0;
    int32 PendingSampleRate = 0;
    int64 FrameCounter = 0;
    bool bStreamActive = false;
    bool bFlushRemainder = false;
    bool bHasPlaybackClock = false;
    double NextPlaybackTimeSeconds = 0.0;
};

class FTalkBotRealtimeAudioLiveLinkSource : public FMetaHumanLocalLiveLinkSource
{
public:
    virtual FText GetSourceType() const override
    {
        return FText::FromString(TEXT("TalkBot (Realtime Audio)"));
    }

    virtual TSubclassOf<ULiveLinkSourceSettings> GetSettingsClass() const override
    {
        return UMetaHumanAudioLiveLinkSourceSettings::StaticClass();
    }

    void PushPcm16(const TArray<uint8>& PcmBytes, const int32 SampleRate)
    {
        if (TSharedPtr<FTalkBotRealtimeAudioLiveLinkSubject> Subject = ActiveSubject.Pin())
        {
            Subject->PushPcm16(PcmBytes, SampleRate);
        }
    }

    void StartStream()
    {
        if (TSharedPtr<FTalkBotRealtimeAudioLiveLinkSubject> Subject = ActiveSubject.Pin())
        {
            Subject->StartStream();
        }
    }

    void FinishStream()
    {
        if (TSharedPtr<FTalkBotRealtimeAudioLiveLinkSubject> Subject = ActiveSubject.Pin())
        {
            Subject->FinishStream();
        }
    }

protected:
    virtual TSharedPtr<FMetaHumanLocalLiveLinkSubject> CreateSubject(const FName& InSubjectName, UMetaHumanLocalLiveLinkSubjectSettings* InSettings) override
    {
        TSharedPtr<FTalkBotRealtimeAudioLiveLinkSubject> Subject = MakeShared<FTalkBotRealtimeAudioLiveLinkSubject>(
            LiveLinkClient,
            SourceGuid,
            InSubjectName,
            Cast<UMetaHumanAudioBaseLiveLinkSubjectSettings>(InSettings)
        );
        ActiveSubject = Subject;
        return Subject;
    }

private:
    TWeakPtr<FTalkBotRealtimeAudioLiveLinkSubject> ActiveSubject;
};

class FTalkBotRealtimeMetaHumanController
{
public:
    explicit FTalkBotRealtimeMetaHumanController(ATalkBotBridgeActor& InOwner)
        : Owner(&InOwner)
    {
    }

    ~FTalkBotRealtimeMetaHumanController()
    {
        Shutdown();
    }

    bool Initialize()
    {
        ATalkBotBridgeActor* OwnerPtr = Owner.Get();
        if (!OwnerPtr)
        {
            return false;
        }

        if (Source.IsValid())
        {
            return SubjectKey.Source.IsValid();
        }

        if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("Live Link client is not available")
            ));
            return false;
        }

        AActor* MetaHumanActor = OwnerPtr->FindMetaHumanActor();
        if (!MetaHumanActor)
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("could not find Hana in the current level")
            ));
            return false;
        }

        ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);

        Source = MakeShared<FTalkBotRealtimeAudioLiveLinkSource>();
        const FGuid SourceGuid = LiveLinkClient.AddSource(Source);
        if (!SourceGuid.IsValid())
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("failed to add the TalkBot realtime Live Link source")
            ));
            Source.Reset();
            return false;
        }

        UMetaHumanAudioLiveLinkSourceSettings* SourceSettings = Cast<UMetaHumanAudioLiveLinkSourceSettings>(LiveLinkClient.GetSourceSettings(SourceGuid));
        if (!SourceSettings)
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("failed to access Live Link source settings")
            ));
            Shutdown();
            return false;
        }

        UMetaHumanAudioBaseLiveLinkSubjectSettings* InitialSubjectSettings = Source->CreateSubjectSettings<UMetaHumanAudioBaseLiveLinkSubjectSettings>();
        InitialSubjectSettings->Mood = ParseRealtimeMood(OwnerPtr->ResolveRealtimeMood());
        InitialSubjectSettings->MoodIntensity = OwnerPtr->ResolveRealtimeMoodIntensity();
        InitialSubjectSettings->Lookahead = FMath::Clamp((OwnerPtr->RealtimeLookahead / 20) * 20, 80, 240);

        SubjectKey = SourceSettings->RequestSubjectCreation(OwnerPtr->RealtimeLiveLinkSubjectName.ToString(), InitialSubjectSettings);
        if (!SubjectKey.Source.IsValid())
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("failed to create the TalkBot realtime Live Link subject")
            ));
            Shutdown();
            return false;
        }

        LiveLinkClient.SetSubjectEnabled(SubjectKey, true);
        this->SubjectSettings = InitialSubjectSettings;
        OwnerPtr->RefreshRealtimeMoodSettings();

        if (!OwnerPtr->ConfigureMetaHumanForRealtimeLiveLink(MetaHumanActor))
        {
            OwnerPtr->OnBridgeWarning.Broadcast(OwnerPtr->BuildBridgeMessage(
                TEXT("realtime-lipsync"),
                TEXT("Hana was found, but its Live Link subject could not be configured automatically")
            ));
        }

        return true;
    }

    void Shutdown()
    {
        if (!Source.IsValid())
        {
            SubjectKey = FLiveLinkSubjectKey();
            SubjectSettings = nullptr;
            return;
        }

        if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
        {
            ILiveLinkClient& LiveLinkClient = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
            Source->RequestSourceShutdown();
            LiveLinkClient.RemoveSource(Source->GetSourceGuid());
        }

        SubjectKey = FLiveLinkSubjectKey();
        SubjectSettings = nullptr;
        Source.Reset();
    }

    void PushPcm16(const TArray<uint8>& PcmBytes, const int32 SampleRate)
    {
        if (Source.IsValid())
        {
            Source->PushPcm16(PcmBytes, SampleRate);
        }
    }

    void StartStream()
    {
        if (Source.IsValid())
        {
            Source->StartStream();
        }
    }

    void FinishStream()
    {
        if (Source.IsValid())
        {
            Source->FinishStream();
        }
    }

    void UpdateSubjectSettings(const ETalkBotLipsyncMood Mood, const float MoodIntensity, const int32 Lookahead)
    {
        if (!SubjectSettings.IsValid())
        {
            return;
        }

        SubjectSettings->Mood = ParseRealtimeMood(Mood);
        SubjectSettings->MoodIntensity = FMath::Clamp(MoodIntensity, 0.0f, 1.0f);
        SubjectSettings->Lookahead = FMath::Clamp((Lookahead / 20) * 20, 80, 240);
    }

private:
    TWeakObjectPtr<ATalkBotBridgeActor> Owner;
    TSharedPtr<FTalkBotRealtimeAudioLiveLinkSource> Source;
    FLiveLinkSubjectKey SubjectKey;
    TWeakObjectPtr<UMetaHumanAudioBaseLiveLinkSubjectSettings> SubjectSettings;
};

ATalkBotBridgeActor::ATalkBotBridgeActor()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);

    AudioComponent = CreateDefaultSubobject<UAudioComponent>(TEXT("BridgeAudio"));
    AudioComponent->SetupAttachment(SceneRoot);
    AudioComponent->bAutoActivate = false;
    AudioComponent->bIsUISound = true;
    AudioComponent->bAllowSpatialization = false;

    CurrentTurnId = 0;
    CurrentSampleRate = DefaultSampleRate;
    bAudioPlaybackStarted = false;
    bPendingResponseRequest = false;
    ActiveSequenceActor = nullptr;
    ActiveSequencePlayer = nullptr;
    RealtimeMetaHumanController = nullptr;
    RealtimeSpeechEnergy = 0.0f;
    RealtimeSpeechEnergyTarget = 0.0f;
    AppliedRealtimeMood = ETalkBotLipsyncMood::Neutral;
    AppliedRealtimeMoodIntensity = 0.0f;
}

ATalkBotBridgeActor::~ATalkBotBridgeActor()
{
    ShutdownRealtimeMetaHumanController();
}

void ATalkBotBridgeActor::BeginPlay()
{
    Super::BeginPlay();

    ApplyLightweightRuntimeMode();

    if (bUseRealtimeMetaHumanLipsync)
    {
        EnsureRealtimeMetaHumanController();
    }

    if (bAutoConnect)
    {
        ConnectToBridge();
    }
}

void ATalkBotBridgeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DisconnectFromBridge();
    Super::EndPlay(EndPlayReason);
}

void ATalkBotBridgeActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float PreviousEnergy = RealtimeSpeechEnergy;
    RealtimeSpeechEnergy = FMath::FInterpTo(RealtimeSpeechEnergy, RealtimeSpeechEnergyTarget, DeltaSeconds, 6.0f);
    if (FMath::Abs(RealtimeSpeechEnergy - PreviousEnergy) > 0.02f)
    {
        RefreshRealtimeMoodSettings();
    }
}

void ATalkBotBridgeActor::ConnectToBridge()
{
    if (WebSocket.IsValid())
    {
        return;
    }

    FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    WebSocket = FWebSocketsModule::Get().CreateWebSocket(BridgeUrl);

    WebSocket->OnConnected().AddWeakLambda(this, [this]()
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            OnBridgeConnected.Broadcast();
            ReceiveBridgeConnected();
            FlushPendingMessages();
        });
    });

    WebSocket->OnConnectionError().AddWeakLambda(this, [this](const FString& Error)
    {
        AsyncTask(ENamedThreads::GameThread, [this, Error]()
        {
            OnBridgeError.Broadcast(BuildBridgeMessage(TEXT("connect"), Error));
        });
    });

    WebSocket->OnClosed().AddWeakLambda(this, [this](int32 StatusCode, const FString& Reason, bool bWasClean)
    {
        AsyncTask(ENamedThreads::GameThread, [this, StatusCode, Reason, bWasClean]()
        {
            const FString Message = FString::Printf(
                TEXT("closed code=%d clean=%s reason=%s"),
                StatusCode,
                bWasClean ? TEXT("true") : TEXT("false"),
                *Reason
            );
            OnBridgeDisconnected.Broadcast(Message);
        });
    });

    WebSocket->OnMessage().AddWeakLambda(this, [this](const FString& Message)
    {
        AsyncTask(ENamedThreads::GameThread, [this, Message]()
        {
            HandleSocketMessage(Message);
        });
    });

    WebSocket->Connect();
}

void ATalkBotBridgeActor::DisconnectFromBridge()
{
    if (AudioComponent)
    {
        AudioComponent->Stop();
    }

    if (ProceduralSoundWave)
    {
        ProceduralSoundWave->ResetAudio();
    }

    StopSequencePlayback();
    ShutdownRealtimeMetaHumanController();
    bAudioPlaybackStarted = false;
    RealtimeTranscriptAccumulator.Reset();
    RealtimeSpeechEnergy = 0.0f;
    RealtimeSpeechEnergyTarget = 0.0f;
    AppliedRealtimeMood = ETalkBotLipsyncMood::Neutral;
    AppliedRealtimeMoodIntensity = 0.0f;

    if (WebSocket.IsValid())
    {
        if (WebSocket->IsConnected())
        {
            WebSocket->Close();
        }
        WebSocket.Reset();
    }
}

bool ATalkBotBridgeActor::IsConnected() const
{
    return WebSocket.IsValid() && WebSocket->IsConnected();
}

void ATalkBotBridgeActor::SendInputText(const FString& Text)
{
    if (Text.IsEmpty())
    {
        return;
    }

    if (!IsConnected())
    {
        PendingInputTexts.Add(Text);
        ConnectToBridge();
        return;
    }

    TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("text"), Text);
    SendJsonMessage(TEXT("input_text"), Payload);
}

void ATalkBotBridgeActor::RequestResponse()
{
    if (!IsConnected())
    {
        bPendingResponseRequest = true;
        ConnectToBridge();
        return;
    }

    SendJsonMessage(TEXT("response.create"), MakeShared<FJsonObject>());
}

UAudioComponent* ATalkBotBridgeActor::GetAudioComponent() const
{
    return AudioComponent;
}

void ATalkBotBridgeActor::SendJsonMessage(const FString& Type, const TSharedPtr<FJsonObject>& Payload) const
{
    if (!IsConnected())
    {
        return;
    }

    TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();
    RootObject->SetStringField(TEXT("type"), Type);
    RootObject->SetObjectField(TEXT("payload"), Payload.ToSharedRef());

    FString Serialized;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
    FJsonSerializer::Serialize(RootObject, Writer);
    WebSocket->Send(Serialized);
}

void ATalkBotBridgeActor::HandleSocketMessage(const FString& Message)
{
    TSharedPtr<FJsonObject> MessageObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, MessageObject) || !MessageObject.IsValid())
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(TEXT("json"), TEXT("invalid message from bridge")));
        return;
    }

    HandleJsonMessage(MessageObject);
}

void ATalkBotBridgeActor::HandleJsonMessage(const TSharedPtr<FJsonObject>& MessageObject)
{
    FString Type;
    if (!MessageObject->TryGetStringField(TEXT("type"), Type))
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(TEXT("json"), TEXT("message missing type")));
        return;
    }

    TSharedPtr<FJsonObject> Payload;
    const TSharedPtr<FJsonObject>* PayloadObject = nullptr;
    if (MessageObject->TryGetObjectField(TEXT("payload"), PayloadObject) && PayloadObject)
    {
        Payload = *PayloadObject;
    }
    if (!Payload.IsValid())
    {
        Payload = MakeShared<FJsonObject>();
    }

    if (Type == TEXT("bridge.ready"))
    {
        OnBridgeReady.Broadcast();
        ReceiveBridgeReady();
        return;
    }

    if (Type == TEXT("session.created"))
    {
        OnSessionCreated.Broadcast();
        return;
    }

    if (Type == TEXT("assistant.response_started"))
    {
        double TurnIdNumber = 0.0;
        if (Payload->TryGetNumberField(TEXT("turnId"), TurnIdNumber))
        {
            CurrentTurnId = static_cast<int32>(TurnIdNumber);
        }

        LatestTranscript.Reset();
        RealtimeTranscriptAccumulator.Reset();
        RealtimeSpeechEnergy = 0.0f;
        RealtimeSpeechEnergyTarget = 0.0f;
        StopSequencePlayback();

        if (ShouldUseStreamedAudio())
        {
            ResetAudioStream(DefaultSampleRate);
        }

        if (bUseRealtimeMetaHumanLipsync)
        {
            EnsureRealtimeMetaHumanController();
            RefreshRealtimeMoodSettings();
            if (RealtimeMetaHumanController)
            {
                RealtimeMetaHumanController->StartStream();
            }
        }

        OnAssistantResponseStarted.Broadcast(CurrentTurnId);
        ReceiveAssistantResponseStarted(CurrentTurnId);
        return;
    }

    if (Type == TEXT("assistant.audio_chunk"))
    {
        if (!ShouldUseStreamedAudio())
        {
            return;
        }

        FString Encoding;
        FString AudioBase64;
        double SampleRateNumber = DefaultSampleRate;
        Payload->TryGetStringField(TEXT("encoding"), Encoding);
        Payload->TryGetStringField(TEXT("audioBase64"), AudioBase64);
        Payload->TryGetNumberField(TEXT("sampleRate"), SampleRateNumber);

        if (!Encoding.Equals(TEXT("pcm16"), ESearchCase::IgnoreCase))
        {
            OnBridgeWarning.Broadcast(BuildBridgeMessage(TEXT("audio"), FString::Printf(TEXT("unsupported encoding %s"), *Encoding)));
            return;
        }

        TArray<uint8> DecodedPcm;
        if (!FBase64::Decode(AudioBase64, DecodedPcm))
        {
            OnBridgeWarning.Broadcast(BuildBridgeMessage(TEXT("audio"), TEXT("failed to decode assistant audio chunk")));
            return;
        }

        const int32 SampleRate = static_cast<int32>(SampleRateNumber);
        EnsureAudioStream(SampleRate);
        if (ProceduralSoundWave)
        {
            ProceduralSoundWave->QueueAudio(DecodedPcm.GetData(), DecodedPcm.Num());
        }

        if (AudioComponent && !bAudioPlaybackStarted)
        {
            bAudioPlaybackStarted = true;
            AudioComponent->Play();
        }

        if (bUseRealtimeMetaHumanLipsync)
        {
            UpdateRealtimeSpeechEnergyFromPcm(DecodedPcm);
            PushRealtimeMetaHumanAudio(DecodedPcm, SampleRate);
        }
        return;
    }

    if (Type == TEXT("assistant.transcript_event"))
    {
        UpdateRealtimeMoodFromText(ExtractTranscriptTextFromPayload(Payload), false);
        return;
    }

    if (Type == TEXT("assistant.transcript_final"))
    {
        FString Transcript;
        double TurnIdNumber = CurrentTurnId;
        Payload->TryGetStringField(TEXT("transcript"), Transcript);
        Payload->TryGetNumberField(TEXT("turnId"), TurnIdNumber);
        LatestTranscript = Transcript;
        UpdateRealtimeMoodFromText(Transcript, true);
        OnAssistantTranscriptFinal.Broadcast(static_cast<int32>(TurnIdNumber), Transcript);
        ReceiveAssistantTranscriptFinal(static_cast<int32>(TurnIdNumber), Transcript);
        return;
    }

    if (Type == TEXT("assistant.response_finished"))
    {
        double TurnIdNumber = CurrentTurnId;
        FString Transcript = LatestTranscript;
        FString WavPath;

        Payload->TryGetNumberField(TEXT("turnId"), TurnIdNumber);
        Payload->TryGetStringField(TEXT("transcript"), Transcript);
        Payload->TryGetStringField(TEXT("wavPath"), WavPath);

        OnAssistantResponseFinished.Broadcast(static_cast<int32>(TurnIdNumber), Transcript, WavPath);
        ReceiveAssistantResponseFinished(static_cast<int32>(TurnIdNumber), Transcript, WavPath);

        if (!bUseRealtimeMetaHumanLipsync && bAutoPlayFinalResponseSequence && !WavPath.IsEmpty())
        {
            HandleFinalResponseSequence(static_cast<int32>(TurnIdNumber), WavPath);
        }

        if (bUseRealtimeMetaHumanLipsync && RealtimeMetaHumanController)
        {
            RealtimeMetaHumanController->FinishStream();
        }

        RealtimeSpeechEnergyTarget = 0.0f;
        return;
    }

    if (Type == TEXT("bridge.warning"))
    {
        FString Source;
        FString Message;
        Payload->TryGetStringField(TEXT("source"), Source);
        Payload->TryGetStringField(TEXT("message"), Message);
        OnBridgeWarning.Broadcast(BuildBridgeMessage(Source, Message));
        return;
    }

    if (Type == TEXT("bridge.error"))
    {
        FString Source;
        FString Message;
        Payload->TryGetStringField(TEXT("source"), Source);
        Payload->TryGetStringField(TEXT("message"), Message);
        OnBridgeError.Broadcast(BuildBridgeMessage(Source, Message));
        return;
    }
}

void ATalkBotBridgeActor::ResetAudioStream(int32 InSampleRate)
{
    CurrentSampleRate = InSampleRate > 0 ? InSampleRate : DefaultSampleRate;

    if (!ProceduralSoundWave)
    {
        ProceduralSoundWave = NewObject<USoundWaveProcedural>(this);
        ProceduralSoundWave->NumChannels = 1;
        ProceduralSoundWave->Duration = INDEFINITELY_LOOPING_DURATION;
        ProceduralSoundWave->SoundGroup = SOUNDGROUP_Voice;
        ProceduralSoundWave->bLooping = false;
    }

    ProceduralSoundWave->ResetAudio();
    ProceduralSoundWave->SetSampleRate(CurrentSampleRate);

    if (AudioComponent)
    {
        AudioComponent->Stop();
        if (AudioComponent->Sound != ProceduralSoundWave)
        {
            AudioComponent->SetSound(ProceduralSoundWave);
        }
        AudioComponent->SetVolumeMultiplier(VolumeMultiplier);
    }

    bAudioPlaybackStarted = false;
}

void ATalkBotBridgeActor::EnsureAudioStream(int32 InSampleRate)
{
    const int32 SafeSampleRate = InSampleRate > 0 ? InSampleRate : DefaultSampleRate;
    if (!ProceduralSoundWave || CurrentSampleRate != SafeSampleRate)
    {
        ResetAudioStream(SafeSampleRate);
    }
}

void ATalkBotBridgeActor::FlushPendingMessages()
{
    if (!IsConnected())
    {
        return;
    }

    for (const FString& PendingText : PendingInputTexts)
    {
        TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
        Payload->SetStringField(TEXT("text"), PendingText);
        SendJsonMessage(TEXT("input_text"), Payload);
    }
    PendingInputTexts.Reset();

    if (bPendingResponseRequest)
    {
        SendJsonMessage(TEXT("response.create"), MakeShared<FJsonObject>());
        bPendingResponseRequest = false;
    }
}

FString ATalkBotBridgeActor::BuildBridgeMessage(const FString& Source, const FString& Message) const
{
    if (Source.IsEmpty())
    {
        return Message;
    }

    return FString::Printf(TEXT("%s: %s"), *Source, *Message);
}

bool ATalkBotBridgeActor::ShouldUseStreamedAudio() const
{
    return bPlayStreamedAudioChunks || bUseRealtimeMetaHumanLipsync;
}

void ATalkBotBridgeActor::ApplyLightweightRuntimeMode()
{
    if (!bApplyLightweightRuntimeMode || !GetWorld())
    {
        return;
    }

    auto ExecConsoleCommand = [this](const FString& Command)
    {
        if (GEngine && GetWorld())
        {
            GEngine->Exec(GetWorld(), *Command);
        }
    };

    ExecConsoleCommand(FString::Printf(TEXT("r.ScreenPercentage %d"), FMath::Clamp(LightweightScreenPercentage, 50, 100)));
    ExecConsoleCommand(TEXT("sg.PostProcessQuality 1"));
    ExecConsoleCommand(TEXT("sg.ShadowQuality 1"));
    ExecConsoleCommand(TEXT("sg.EffectsQuality 1"));
    ExecConsoleCommand(TEXT("sg.TextureQuality 2"));
    ExecConsoleCommand(TEXT("r.SSR.Quality 1"));

    if (bLightweightDisableLumen)
    {
        ExecConsoleCommand(TEXT("sg.GlobalIlluminationQuality 1"));
        ExecConsoleCommand(TEXT("sg.ReflectionQuality 1"));
        ExecConsoleCommand(TEXT("r.Lumen.DiffuseIndirect.Allow 0"));
        ExecConsoleCommand(TEXT("r.Lumen.Reflections.Allow 0"));
    }

    if (bLightweightDisableVolumetricClouds)
    {
        ExecConsoleCommand(TEXT("r.VolumetricCloud 0"));
    }

    if (bLightweightDisableStrands)
    {
        ExecConsoleCommand(TEXT("r.HairStrands.Strands 0"));
    }

    if (AActor* MetaHumanActor = FindMetaHumanActor())
    {
        TInlineComponentArray<UActorComponent*> Components;
        MetaHumanActor->GetComponents(Components);

        for (UActorComponent* Component : Components)
        {
            if (ULODSyncComponent* LODSyncComponent = Cast<ULODSyncComponent>(Component))
            {
                LODSyncComponent->ForcedLOD = FMath::Clamp(LightweightForcedLOD, 0, 8);
                LODSyncComponent->MinLOD = FMath::Max(0, LightweightForcedLOD - 1);
                LODSyncComponent->UpdateLOD();
            }

            if (USkinnedMeshComponent* SkinnedMeshComponent = Cast<USkinnedMeshComponent>(Component))
            {
                SkinnedMeshComponent->SetMinLOD(FMath::Max(0, LightweightForcedLOD - 1));
                SkinnedMeshComponent->SetForcedLOD(FMath::Clamp(LightweightForcedLOD, 0, 8));
            }
        }

        UE_LOG(
            LogTalkBotBridge,
            Display,
            TEXT("Applied lightweight runtime mode: screen=%d forcedLOD=%d strands=%s lumen=%s clouds=%s actor=%s"),
            FMath::Clamp(LightweightScreenPercentage, 50, 100),
            FMath::Clamp(LightweightForcedLOD, 0, 8),
            bLightweightDisableStrands ? TEXT("off") : TEXT("on"),
            bLightweightDisableLumen ? TEXT("off") : TEXT("on"),
            bLightweightDisableVolumetricClouds ? TEXT("off") : TEXT("on"),
            *MetaHumanActor->GetName()
        );
    }
}

void ATalkBotBridgeActor::EnsureRealtimeMetaHumanController()
{
    if (!bUseRealtimeMetaHumanLipsync)
    {
        return;
    }

    if (!RealtimeMetaHumanController)
    {
        RealtimeMetaHumanController = new FTalkBotRealtimeMetaHumanController(*this);
    }

    if (!RealtimeMetaHumanController->Initialize())
    {
        delete RealtimeMetaHumanController;
        RealtimeMetaHumanController = nullptr;
    }
}

void ATalkBotBridgeActor::ShutdownRealtimeMetaHumanController()
{
    if (RealtimeMetaHumanController)
    {
        RealtimeMetaHumanController->Shutdown();
        delete RealtimeMetaHumanController;
        RealtimeMetaHumanController = nullptr;
    }
}

void ATalkBotBridgeActor::PushRealtimeMetaHumanAudio(const TArray<uint8>& DecodedPcm, const int32 SampleRate)
{
    if (!bUseRealtimeMetaHumanLipsync)
    {
        return;
    }

    if (!RealtimeMetaHumanController)
    {
        EnsureRealtimeMetaHumanController();
    }

    if (RealtimeMetaHumanController)
    {
        static int32 LoggedChunkCount = 0;
        if (LoggedChunkCount < 5)
        {
            UE_LOG(
                LogTalkBotBridge,
                Display,
                TEXT("Pushing realtime lipsync audio chunk: bytes=%d sampleRate=%d"),
                DecodedPcm.Num(),
                SampleRate
            );
            ++LoggedChunkCount;
        }
        RealtimeMetaHumanController->PushPcm16(DecodedPcm, SampleRate);
    }
}

void ATalkBotBridgeActor::UpdateRealtimeSpeechEnergyFromPcm(const TArray<uint8>& DecodedPcm)
{
    if (!bUseRealtimeMetaHumanLipsync)
    {
        return;
    }

    RealtimeSpeechEnergyTarget = ComputePcmEnergy(DecodedPcm);
    RefreshRealtimeMoodSettings();
}

void ATalkBotBridgeActor::UpdateRealtimeMoodFromText(const FString& TranscriptDelta, const bool bIsFinalText)
{
    if (!bUseRealtimeMetaHumanLipsync || TranscriptDelta.IsEmpty())
    {
        return;
    }

    if (bIsFinalText)
    {
        RealtimeTranscriptAccumulator = TranscriptDelta;
    }
    else
    {
        RealtimeTranscriptAccumulator += TranscriptDelta;
        if (RealtimeTranscriptAccumulator.Len() > RealtimeTranscriptWindowChars * 2)
        {
            RealtimeTranscriptAccumulator.RightInline(RealtimeTranscriptWindowChars, EAllowShrinking::No);
        }
    }

    RefreshRealtimeMoodSettings();
}

void ATalkBotBridgeActor::RefreshRealtimeMoodSettings()
{
    if (!bUseRealtimeMetaHumanLipsync || !RealtimeMetaHumanController)
    {
        return;
    }

    const ETalkBotLipsyncMood ResolvedMood = ResolveRealtimeMood();
    const float ResolvedIntensity = ResolveRealtimeMoodIntensity();

    if (ResolvedMood == AppliedRealtimeMood && FMath::IsNearlyEqual(ResolvedIntensity, AppliedRealtimeMoodIntensity, 0.025f))
    {
        return;
    }

    AppliedRealtimeMood = ResolvedMood;
    AppliedRealtimeMoodIntensity = ResolvedIntensity;
    RealtimeMetaHumanController->UpdateSubjectSettings(ResolvedMood, ResolvedIntensity, RealtimeLookahead);
}

ETalkBotLipsyncMood ATalkBotBridgeActor::ResolveRealtimeMood() const
{
    if (RealtimeMood != ETalkBotLipsyncMood::AutoDetect)
    {
        return RealtimeMood;
    }

    return InferMoodFromTranscript(RealtimeTranscriptAccumulator);
}

float ATalkBotBridgeActor::ResolveRealtimeMoodIntensity() const
{
    const float TranscriptBoost = ComputeTranscriptMoodBoost(RealtimeTranscriptAccumulator);
    const float AudioBoost = FMath::Max(RealtimeSpeechEnergy, RealtimeSpeechEnergyTarget) * FMath::Clamp(RealtimeAudioIntensityBoost, 0.0f, 1.0f);
    return FMath::Clamp(RealtimeMoodIntensity + TranscriptBoost + AudioBoost, 0.0f, 1.0f);
}

bool ATalkBotBridgeActor::ConfigureMetaHumanForRealtimeLiveLink(AActor* MetaHumanActor) const
{
    if (!MetaHumanActor)
    {
        return false;
    }

    const FString SubjectString = RealtimeLiveLinkSubjectName.ToString();

    auto ConfigureObject = [this, &SubjectString](UObject* Object)
    {
        if (!Object)
        {
            return false;
        }

        bool bConfiguredAny = false;
        bConfiguredAny |= SetBoolPropertyByName(Object, TEXT("UseLiveLink"), true);
        bConfiguredAny |= SetBoolPropertyByName(Object, TEXT("UseARKitFace"), true);
        bConfiguredAny |= SetBoolPropertyByName(Object, TEXT("UseLiveLinkBody"), false);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("ARKitFaceSubj"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LiveLinkSubjectName"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LLink_Face_Subj"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("FaceSubjectName"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LiveLinkFaceSubject"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("SubjectName"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LiveLinkBodySubj"), NAME_None, FString());
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LLink_Body_Subj"), NAME_None, FString());
        bConfiguredAny |= SetNameOrStringPropertyByName(Object, TEXT("LiveLinkBodySubject"), NAME_None, FString());
        bConfiguredAny |= CallSingleBoolFunction(Object, TEXT("SetUseLiveLink"), true);
        bConfiguredAny |= CallSingleBoolFunction(Object, TEXT("SetUseARKitFace"), true);
        bConfiguredAny |= CallSingleBoolFunction(Object, TEXT("SetUseLiveLinkBody"), false);
        bConfiguredAny |= CallSingleNameOrStringFunction(Object, TEXT("SetSubject"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= CallSingleNameOrStringFunction(Object, TEXT("SetSubjectName"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= CallSingleNameOrStringFunction(Object, TEXT("SetLiveLinkSubject"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= CallSingleNameOrStringFunction(Object, TEXT("SetLiveLinkFaceSubject"), RealtimeLiveLinkSubjectName, SubjectString);
        bConfiguredAny |= CallNoArgFunction(Object, TEXT("ARKitFaceSetup"));
        bConfiguredAny |= CallNoArgFunction(Object, TEXT("LiveLinkSetup"));
        bConfiguredAny |= CallNoArgFunction(Object, TEXT("LiveRetargetSetup"));
        bConfiguredAny |= CallNoArgFunction(Object, TEXT("SetupLiveLink"));
        bConfiguredAny |= CallNoArgFunction(Object, TEXT("SetupARKitFace"));

        return bConfiguredAny;
    };

    bool bConfigured = ConfigureObject(MetaHumanActor);

    TInlineComponentArray<UActorComponent*> Components;
    MetaHumanActor->GetComponents(Components);
    for (UActorComponent* Component : Components)
    {
        bConfigured |= ConfigureObject(Component);
    }

    UE_LOG(
        LogTalkBotBridge,
        Display,
        TEXT("Realtime Live Link binding for %s: subject=%s configured=%s"),
        *MetaHumanActor->GetName(),
        *SubjectString,
        bConfigured ? TEXT("true") : TEXT("false")
    );

    return bConfigured;
}

void ATalkBotBridgeActor::StopSequencePlayback()
{
    if (ActiveSequencePlayer)
    {
        ActiveSequencePlayer->Stop();
        ActiveSequencePlayer = nullptr;
    }

    if (ActiveSequenceActor)
    {
        ActiveSequenceActor->Destroy();
        ActiveSequenceActor = nullptr;
    }
}

void ATalkBotBridgeActor::HandleFinalResponseSequence(int32 TurnId, const FString& WavPath)
{
    if (AudioComponent)
    {
        AudioComponent->Stop();
    }

    if (ProceduralSoundWave)
    {
        ProceduralSoundWave->ResetAudio();
    }

    AActor* MetaHumanActor = FindMetaHumanActor();
    if (!MetaHumanActor)
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(
            TEXT("lipsync"),
            FString::Printf(TEXT("turn %d could not find a Hana actor in the level"), TurnId)
        ));
        return;
    }

    ULevelSequence* LevelSequence = ResolveLevelSequenceForWav(WavPath);
    if (!LevelSequence)
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(
            TEXT("lipsync"),
            FString::Printf(TEXT("turn %d could not load or generate a Level Sequence for %s"), TurnId, *WavPath)
        ));
        return;
    }

    USoundWave* SoundWave = LoadObject<USoundWave>(nullptr, *BuildPreferredSoundWavePath(StorageRoot, WavPath));

    if (!PlayLevelSequenceOnMetaHuman(LevelSequence, MetaHumanActor, SoundWave))
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(
            TEXT("lipsync"),
            FString::Printf(TEXT("turn %d failed to play Level Sequence %s"), TurnId, *LevelSequence->GetPathName())
        ));
    }
}

AActor* ATalkBotBridgeActor::FindMetaHumanActor() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (!MetaHumanActorTag.IsNone())
    {
        TArray<AActor*> TaggedActors;
        UGameplayStatics::GetAllActorsWithTag(World, MetaHumanActorTag, TaggedActors);
        if (TaggedActors.Num() > 0)
        {
            return TaggedActors[0];
        }
    }

    if (!MetaHumanClassPath.IsEmpty())
    {
        if (UClass* MetaHumanClass = LoadClass<AActor>(nullptr, *MetaHumanClassPath))
        {
            TArray<AActor*> MatchingActors;
            UGameplayStatics::GetAllActorsOfClass(World, MetaHumanClass, MatchingActors);
            if (MatchingActors.Num() > 0)
            {
                return MatchingActors[0];
            }
        }
    }

    TArray<AActor*> AllActors;
    UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
    for (AActor* Actor : AllActors)
    {
        if (Actor && Actor->GetName().Contains(TEXT("Hana"), ESearchCase::IgnoreCase))
        {
            return Actor;
        }
    }

    return nullptr;
}

ULevelSequence* ATalkBotBridgeActor::ResolveLevelSequenceForWav(const FString& WavPath)
{
    const FString PreferredSequencePath = BuildPreferredLevelSequencePath(StorageRoot, WavPath);
    if (ULevelSequence* ExistingLevelSequence = LoadObject<ULevelSequence>(nullptr, *PreferredSequencePath))
    {
        EnsureMetaHumanBindingTag(ExistingLevelSequence, MetaHumanBindingTag);
        return ExistingLevelSequence;
    }

    if (!bGenerateLevelSequenceIfMissing)
    {
        return nullptr;
    }

#if WITH_EDITOR
    FString ErrorMessage;
    ULevelSequence* GeneratedLevelSequence = GenerateLevelSequenceFromWav(
        WavPath,
        MetaHumanAssetPath,
        StorageRoot,
        MetaHumanBindingTag,
        GenerationMood,
        GenerationProcessMask,
        GenerationHeadMovement,
        bPersistGeneratedAssets,
        ErrorMessage
    );

    if (!GeneratedLevelSequence && !ErrorMessage.IsEmpty())
    {
        OnBridgeWarning.Broadcast(BuildBridgeMessage(TEXT("lipsync"), ErrorMessage));
    }

    return GeneratedLevelSequence;
#else
    return nullptr;
#endif
}

bool ATalkBotBridgeActor::PlayLevelSequenceOnMetaHuman(ULevelSequence* LevelSequence, AActor* MetaHumanActor, USoundWave* SoundWave)
{
    if (!LevelSequence || !MetaHumanActor || !GetWorld())
    {
        return false;
    }

    SanitizeLevelSequenceForRuntimePlayback(LevelSequence);

    if (!EnsureMetaHumanBindingTag(LevelSequence, MetaHumanBindingTag))
    {
        return false;
    }
    StopSequencePlayback();

    FMovieSceneSequencePlaybackSettings PlaybackSettings;
    PlaybackSettings.bAutoPlay = false;
    PlaybackSettings.bDisableCameraCuts = true;

    ALevelSequenceActor* SequenceActor = nullptr;
    ULevelSequencePlayer* SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
        GetWorld(),
        LevelSequence,
        PlaybackSettings,
        SequenceActor
    );

    if (!SequenceActor || !SequencePlayer)
    {
        return false;
    }

    SequencePlayer->SetDisableCameraCuts(true);

    TArray<AActor*> BoundActors;
    BoundActors.Add(MetaHumanActor);
    SequenceActor->SetBindingByTag(MetaHumanBindingTag, BoundActors, false);

    if (AudioComponent)
    {
        AudioComponent->Stop();
        AudioComponent->SetSound(SoundWave);
        AudioComponent->SetVolumeMultiplier(VolumeMultiplier);
    }

    ActiveSequenceActor = SequenceActor;
    ActiveSequencePlayer = SequencePlayer;
    if (AudioComponent && SoundWave)
    {
        AudioComponent->Play(0.0f);
    }
    ActiveSequencePlayer->Play();
    return true;
}
