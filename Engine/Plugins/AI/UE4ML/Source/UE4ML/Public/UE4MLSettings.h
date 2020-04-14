// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "Agents/4MLAgent.h"
#include "4MLSession.h"
#include "4MLManager.h"
#include "UE4MLSettings.generated.h"


class U4MLManager;
class U4MLAgent;
class U4MLSession;

#define GET_CONFIG_VALUE(a) (GetDefault<UUE4MLSettings>()->a)

/**
 * Implements the settings for the UE4ML plugin.
 */
UCLASS(config = Plugins, defaultconfig, DisplayName="UE4ML")
class UE4ML_API UUE4MLSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UUE4MLSettings(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	static TSubclassOf<U4MLManager> GetManagerClass();
	static TSubclassOf<U4MLSession> GetSessionClass();
	static TSubclassOf<U4MLAgent> GetAgentClass();
	static uint16 GetDefaultRPCServerPort() { return GET_CONFIG_VALUE(DefaultRPCServerPort); }

protected:
	UPROPERTY(EditAnywhere, config, Category = UE4ML, meta = (MetaClass = "4MLManager"))
	FSoftClassPath ManagerClass;

	UPROPERTY(EditAnywhere, config, Category = UE4ML, meta = (MetaClass = "4MLSession"))
	FSoftClassPath SessionClass;
	
	UPROPERTY(EditAnywhere, config, Category = UE4ML, meta = (MetaClass = "4MLAgent"))
	FSoftClassPath DefautAgentClass;

	UPROPERTY(EditAnywhere, config, Category = UE4ML)
	uint16 DefaultRPCServerPort = 15151;
};

#undef GET_CONFIG_VALUE