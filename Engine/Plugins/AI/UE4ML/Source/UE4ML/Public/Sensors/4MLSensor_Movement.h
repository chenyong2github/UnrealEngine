// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sensors/4MLSensor.h"
#include "4MLTypes.h"
#include "4MLSensor_Movement.generated.h"


UCLASS(Blueprintable)
class UE4ML_API U4MLSensor_Movement : public U4MLSensor
{
	GENERATED_BODY()
public:
	U4MLSensor_Movement(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual bool ConfigureForAgent(U4MLAgent& Agent) override;
	virtual void Configure(const TMap<FName, FString>& Params) override;
	virtual TSharedPtr<F4ML::FSpace> ConstructSpaceDef() const override;

protected:
	virtual void OnAvatarSet(AActor* Avatar) override;
	virtual void SenseImpl(const float DeltaTime) override;
	virtual void GetObservations(F4MLMemoryWriter& Ar) override;

protected:
	UPROPERTY()
	uint32 bAbsoluteLocation : 1;
	
	UPROPERTY()
	uint32 bAbsoluteVelocity : 1;

	UPROPERTY()
	FVector RefLocation;

	UPROPERTY()
	FVector RefVelocity;

	UPROPERTY()
	FVector CurrentLocation;

	UPROPERTY()
	FVector CurrentVelocity;
};
