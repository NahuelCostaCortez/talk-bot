#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TalkBotBridgeActor.generated.h"

class UAudioComponent;
class ALevelSequenceActor;
class ULevelSequence;
class ULevelSequencePlayer;
class USoundWave;
class USoundWaveProcedural;
class USceneComponent;
class IWebSocket;
class FTalkBotRealtimeMetaHumanController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FTalkBotBridgeConnectedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTalkBotBridgeMessageSignature, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTalkBotBridgeTurnStartedSignature, int32, TurnId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTalkBotBridgeTranscriptSignature, int32, TurnId, const FString&, Transcript);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTalkBotBridgeResponseFinishedSignature, int32, TurnId, const FString&, Transcript, const FString&, WavPath);

UENUM(BlueprintType)
enum class ETalkBotLipsyncMood : uint8
{
    AutoDetect UMETA(DisplayName = "Auto"),
    Neutral,
    Happy,
    Sad,
    Disgust,
    Anger,
    Surprise,
    Fear
};

UENUM(BlueprintType)
enum class ETalkBotLipsyncProcessMask : uint8
{
    FullFace,
    MouthOnly
};

UENUM(BlueprintType)
enum class ETalkBotLipsyncHeadMovement : uint8
{
    Disabled,
    ControlRig
};

UCLASS(Blueprintable)
class MYPROJECT_API ATalkBotBridgeActor : public AActor
{
    GENERATED_BODY()

public:
    ATalkBotBridgeActor();
    virtual ~ATalkBotBridgeActor() override;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UFUNCTION(BlueprintCallable, Category = "TalkBot|Bridge")
    void ConnectToBridge();

    UFUNCTION(BlueprintCallable, Category = "TalkBot|Bridge")
    void DisconnectFromBridge();

    UFUNCTION(BlueprintPure, Category = "TalkBot|Bridge")
    bool IsConnected() const;

    UFUNCTION(BlueprintCallable, Category = "TalkBot|Bridge")
    void SendInputText(const FString& Text);

    UFUNCTION(BlueprintCallable, Category = "TalkBot|Bridge")
    void RequestResponse();

    UFUNCTION(BlueprintPure, Category = "TalkBot|Audio")
    UAudioComponent* GetAudioComponent() const;

    UFUNCTION(BlueprintImplementableEvent, Category = "TalkBot|Events", meta = (DisplayName = "On Bridge Connected"))
    void ReceiveBridgeConnected();

    UFUNCTION(BlueprintImplementableEvent, Category = "TalkBot|Events", meta = (DisplayName = "On Bridge Ready"))
    void ReceiveBridgeReady();

    UFUNCTION(BlueprintImplementableEvent, Category = "TalkBot|Events", meta = (DisplayName = "On Assistant Response Started"))
    void ReceiveAssistantResponseStarted(int32 TurnId);

    UFUNCTION(BlueprintImplementableEvent, Category = "TalkBot|Events", meta = (DisplayName = "On Assistant Transcript Final"))
    void ReceiveAssistantTranscriptFinal(int32 TurnId, const FString& Transcript);

    UFUNCTION(BlueprintImplementableEvent, Category = "TalkBot|Events", meta = (DisplayName = "On Assistant Response Finished"))
    void ReceiveAssistantResponseFinished(int32 TurnId, const FString& Transcript, const FString& WavPath);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Bridge")
    FString BridgeUrl = TEXT("ws://127.0.0.1:8765");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Bridge")
    bool bAutoConnect = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Audio", meta = (ClampMin = "0.0"))
    float VolumeMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Audio")
    bool bPlayStreamedAudioChunks = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Audio", meta = (ClampMin = "8000"))
    int32 DefaultSampleRate = 24000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    bool bAutoPlayFinalResponseSequence = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Realtime")
    bool bUseRealtimeMetaHumanLipsync = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Realtime")
    FName RealtimeLiveLinkSubjectName = TEXT("TalkBotHanaAudio");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Realtime")
    ETalkBotLipsyncMood RealtimeMood = ETalkBotLipsyncMood::Neutral;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Realtime", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float RealtimeMoodIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Realtime", meta = (ClampMin = "80", ClampMax = "240"))
    int32 RealtimeLookahead = 80;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance")
    bool bApplyLightweightRuntimeMode = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance", meta = (ClampMin = "50", ClampMax = "100"))
    int32 LightweightScreenPercentage = 70;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance", meta = (ClampMin = "0", ClampMax = "8"))
    int32 LightweightForcedLOD = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance")
    bool bLightweightDisableStrands = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance")
    bool bLightweightDisableLumen = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Performance")
    bool bLightweightDisableVolumetricClouds = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    bool bGenerateLevelSequenceIfMissing = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    bool bPersistGeneratedAssets = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    FString MetaHumanAssetPath = TEXT("/Game/MetaHumans/Hana/BP_Hana.BP_Hana");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    FString MetaHumanClassPath = TEXT("/Game/MetaHumans/Hana/BP_Hana.BP_Hana_C");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    FString StorageRoot = TEXT("/Game/TalkBot");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    FName MetaHumanBindingTag = TEXT("TalkBotHana");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    FName MetaHumanActorTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    ETalkBotLipsyncMood GenerationMood = ETalkBotLipsyncMood::AutoDetect;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    ETalkBotLipsyncProcessMask GenerationProcessMask = ETalkBotLipsyncProcessMask::MouthOnly;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TalkBot|Lipsync")
    ETalkBotLipsyncHeadMovement GenerationHeadMovement = ETalkBotLipsyncHeadMovement::Disabled;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeConnectedSignature OnBridgeConnected;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeMessageSignature OnBridgeDisconnected;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeMessageSignature OnBridgeWarning;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeMessageSignature OnBridgeError;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeConnectedSignature OnBridgeReady;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeConnectedSignature OnSessionCreated;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeTurnStartedSignature OnAssistantResponseStarted;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeTranscriptSignature OnAssistantTranscriptFinal;

    UPROPERTY(BlueprintAssignable, Category = "TalkBot|Events")
    FTalkBotBridgeResponseFinishedSignature OnAssistantResponseFinished;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TalkBot|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TalkBot|Components")
    TObjectPtr<UAudioComponent> AudioComponent;

    UPROPERTY(Transient)
    TObjectPtr<USoundWaveProcedural> ProceduralSoundWave;

private:
    friend class FTalkBotRealtimeMetaHumanController;

    TSharedPtr<IWebSocket> WebSocket;
    int32 CurrentTurnId;
    int32 CurrentSampleRate;
    FString LatestTranscript;
    bool bAudioPlaybackStarted;
    TArray<FString> PendingInputTexts;
    bool bPendingResponseRequest;
    TObjectPtr<ALevelSequenceActor> ActiveSequenceActor;
    TObjectPtr<ULevelSequencePlayer> ActiveSequencePlayer;
    FTalkBotRealtimeMetaHumanController* RealtimeMetaHumanController;

    void SendJsonMessage(const FString& Type, const TSharedPtr<FJsonObject>& Payload) const;
    void HandleSocketMessage(const FString& Message);
    void HandleJsonMessage(const TSharedPtr<FJsonObject>& MessageObject);
    void ResetAudioStream(int32 InSampleRate);
    void EnsureAudioStream(int32 InSampleRate);
    void FlushPendingMessages();
    FString BuildBridgeMessage(const FString& Source, const FString& Message) const;
    bool ShouldUseStreamedAudio() const;
    void ApplyLightweightRuntimeMode();
    void EnsureRealtimeMetaHumanController();
    void ShutdownRealtimeMetaHumanController();
    void PushRealtimeMetaHumanAudio(const TArray<uint8>& DecodedPcm, int32 SampleRate);
    bool ConfigureMetaHumanForRealtimeLiveLink(AActor* MetaHumanActor) const;
    void StopSequencePlayback();
    void HandleFinalResponseSequence(int32 TurnId, const FString& WavPath);
    AActor* FindMetaHumanActor() const;
    ULevelSequence* ResolveLevelSequenceForWav(const FString& WavPath);
    bool PlayLevelSequenceOnMetaHuman(ULevelSequence* LevelSequence, AActor* MetaHumanActor, USoundWave* SoundWave);
};
