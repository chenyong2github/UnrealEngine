// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Tickable.h"
#include "GameplayDebuggerPlayerManager.generated.h"

class AGameplayDebuggerCategoryReplicator;
class APlayerController;
class UGameplayDebuggerLocalController;
class UInputComponent;

USTRUCT()
struct FGameplayDebuggerPlayerData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TObjectPtr<UGameplayDebuggerLocalController> Controller = nullptr;

	UPROPERTY()
	TObjectPtr<UInputComponent> InputComponent = nullptr;

	UPROPERTY()
	TObjectPtr<AGameplayDebuggerCategoryReplicator> Replicator = nullptr;
};

UCLASS(NotBlueprintable, NotBlueprintType, notplaceable, noteditinlinenew, hidedropdown, Transient)
class GAMEPLAYDEBUGGER_API AGameplayDebuggerPlayerManager : public AActor, public FTickableGameObject
{
	GENERATED_UCLASS_BODY()

	virtual void TickActor(float DeltaTime, enum ELevelTick TickType, FActorTickFunction& ThisTickFunction) override;

	// we're ticking only the manager only when in editor
	// FTickableGameObject begin
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual void Tick(float DeltaTime) override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickable() const override;
	// FTickableGameObject end

	virtual void PostInitProperties() override;

protected:
	virtual void BeginPlay() override;

public:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	void Init();
		
	void UpdateAuthReplicators();
	void RegisterReplicator(AGameplayDebuggerCategoryReplicator& Replicator);
	void RefreshInputBindings(AGameplayDebuggerCategoryReplicator& Replicator);

	AGameplayDebuggerCategoryReplicator* GetReplicator(const APlayerController& OwnerPC) const;
	UInputComponent* GetInputComponent(const APlayerController& OwnerPC) const;
	UGameplayDebuggerLocalController* GetLocalController(const APlayerController& OwnerPC) const;
#if WITH_EDITOR
	UGameplayDebuggerLocalController* GetEditorController() const { return EditorWorldData.Controller; }
#endif // WITH_EDITOR
	
	const FGameplayDebuggerPlayerData* GetPlayerData(const APlayerController& OwnerPC) const;

	static AGameplayDebuggerPlayerManager& GetCurrent(UWorld* World);

	/** extracts view location and direction from a player controller that can be used for picking */
	static void GetViewPoint(const APlayerController& OwnerPC, FVector& OutViewLocation, FVector& OutViewDirection);

protected:

	UPROPERTY()
	TArray<FGameplayDebuggerPlayerData> PlayerData;

	UPROPERTY()
	TArray<TObjectPtr<AGameplayDebuggerCategoryReplicator>> PendingRegistrations;

#if WITH_EDITORONLY_DATA 
	UPROPERTY()
	FGameplayDebuggerPlayerData EditorWorldData;
#endif // WITH_EDITORONLY_DATA

	uint32 bHasAuthority : 1;
	uint32 bIsLocal : 1;
	uint32 bInitialized : 1;
};
