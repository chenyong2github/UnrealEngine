// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AISubsystem.h"
#include "GameplayBehaviorManager.generated.h"


class UGameplayBehavior;
class AActor;
class UGameplayBehaviorConfig;
class UWorld;

USTRUCT()
struct FAgentGameplayBehaviors
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<UGameplayBehavior*> Behaviors;
};

UCLASS(config = Game, defaultconfig, Transient)
class GAMEPLAYBEHAVIORSMODULE_API UGameplayBehaviorManager : public UAISubsystem//, public FSelfRegisteringExec
{
	GENERATED_BODY()
public:
	DECLARE_DELEGATE_RetVal_OneParam(UGameplayBehaviorManager*, FInstanceGetterSignature, UWorld& /*World*/);

	UGameplayBehaviorManager(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void PostInitProperties() override;

	bool StopBehavior(AActor& Avatar, TSubclassOf<UGameplayBehavior> BehaviorToStop);

	static UGameplayBehaviorManager* GetCurrent(UWorld* World);
	static bool TriggerBehavior(const UGameplayBehaviorConfig& Config, AActor& Avatar, AActor* SmartObjectOwner = nullptr);
	static bool TriggerBehavior(UGameplayBehavior& Behavior, AActor& Avatar, const UGameplayBehaviorConfig* Config, AActor* SmartObjectOwner = nullptr);

	bool IsShuttingDown() const { return !IsValidChecked(this) || IsUnreachable(); }

	UWorld* GetWorldFast() const { return Cast<UWorld>(GetOuter()); }

protected:
	void OnBehaviorFinished(UGameplayBehavior& Behavior, AActor& Avatar, const bool bInterrupted);

	virtual bool TriggerBehaviorImpl(UGameplayBehavior& Behavior, AActor& Avatar, const UGameplayBehaviorConfig* Config, AActor* SmartObjectOwner = nullptr);

protected:
	UPROPERTY()
	TMap<AActor*, FAgentGameplayBehaviors> AgentGameplayBehaviors;

	UPROPERTY(config)
	uint32 bCreateIfMissing : 1;

	static FInstanceGetterSignature InstanceGetterDelegate;
};
