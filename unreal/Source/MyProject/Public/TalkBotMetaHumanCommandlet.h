#pragma once

#include "Commandlets/Commandlet.h"

#include "TalkBotMetaHumanCommandlet.generated.h"

UCLASS()
class MYPROJECT_API UTalkBotMetaHumanCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UTalkBotMetaHumanCommandlet();

    virtual int32 Main(const FString& Params) override;
};
