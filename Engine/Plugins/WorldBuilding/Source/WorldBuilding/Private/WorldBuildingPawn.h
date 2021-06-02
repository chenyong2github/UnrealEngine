// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "WorldBuildingPawn.generated.h"

class UInputComponent;
class UStaticMeshComponent;

UCLASS()
class AWorldBuildingPawn : public ACharacter
{
	GENERATED_UCLASS_BODY()

public:
	virtual void SetupPlayerInputComponent(UInputComponent* InInputComponent) override;

	bool IsRunning() const;
	float GetRunningSpeedModifier() const { return RunningSpeedModifier; }

protected:
	void MoveForward(float Val);
	void MoveRight(float Val);
	void MoveUp_World(float Val);
	void TurnAtRate(float Rate);
	void LookUpAtRate(float Rate);

	void OnStartRunning();
	void OnStopRunning();
	void SetRunning(bool bNewRunning);

	UFUNCTION(reliable, server)
	void ServerSetRunning(bool bNewRunning);

	/** Name of the MeshComponent. Use this name if you want to prevent creation of the component (with ObjectInitializer.DoNotCreateDefaultSubobject). */
	static FName MeshComponentName;

public:
	/** The mesh associated with this Pawn. */
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** current running state */
	UPROPERTY(Transient, Replicated)
	uint8 bWantsToRun : 1;

private:
	float BaseTurnRate;
	float BaseLookUpRate;
	float RunningSpeedModifier;
};