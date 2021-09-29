// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "LWComponentTypes.h"
#include "MassComponentHitTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "MassComponentHitSubsystem.generated.h"

class UMassAgentSubsystem;
class UMassSignalSubsystem;
class UCapsuleComponent;


/**
 * Subsystem that keeps track of the latest component hits and allow mass entities to retrieve and handle them
 */
UCLASS()
class MASSAIBEHAVIOR_API UMassComponentHitSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	const FMassHitResult* GetLastHit(const FLWEntity Entity) const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	
	void RegisterForComponentHit(const FLWEntity Entity, UCapsuleComponent& CapsuleComponent);
	void UnregisterForComponentHit(FLWEntity Entity, UCapsuleComponent& CapsuleComponent);

	UFUNCTION()
	void OnHitCallback(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	UPROPERTY()
	UMassSignalSubsystem* SignalSubsystem;

	UPROPERTY()
    UMassAgentSubsystem* AgentSubsystem;

	UPROPERTY()
	TMap<FLWEntity, FMassHitResult> HitResults;

	UPROPERTY()
	TMap<UActorComponent*, FLWEntity> ComponentToEntityMap;

	UPROPERTY()
	TMap<FLWEntity, UActorComponent*> EntityToComponentMap;
};
