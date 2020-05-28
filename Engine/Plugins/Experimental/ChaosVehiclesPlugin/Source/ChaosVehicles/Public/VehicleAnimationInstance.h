// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "VehicleAnimationInstance.generated.h"

class UChaosWheeledVehicleMovementComponent;

struct FWheelAnimationData
{
	FName BoneName;
	FRotator RotOffset;
	FVector LocOffset;
};

/** Proxy override for this UAnimInstance-derived class */
USTRUCT()
	struct CHAOSVEHICLES_API FVehicleAnimationInstanceProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

		FVehicleAnimationInstanceProxy()
		: FAnimInstanceProxy()
	{
	}

	FVehicleAnimationInstanceProxy(UAnimInstance* Instance)
		: FAnimInstanceProxy(Instance)
	{
	}

public:

	void SetWheeledVehicleComponent(const UChaosWheeledVehicleMovementComponent* InWheeledVehicleComponent);

	/** FAnimInstanceProxy interface begin*/
	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	/** FAnimInstanceProxy interface end*/

	const TArray<FWheelAnimationData>& GetWheelAnimData() const
	{
		return WheelInstances;
	}

private:
	TArray<FWheelAnimationData> WheelInstances;
};

UCLASS(transient)
	class CHAOSVEHICLES_API UVehicleAnimationInstance : public UAnimInstance
{
	GENERATED_UCLASS_BODY()

		/** Makes a montage jump to the end of a named section. */
		UFUNCTION(BlueprintCallable, Category = "Animation")
		class AWheeledVehiclePawn* GetVehicle();

public:
	TArray<TArray<FWheelAnimationData>> WheelData;

public:
	void SetWheeledVehicleComponent(const UChaosWheeledVehicleMovementComponent* InWheeledVehicleComponent)
	{
		WheeledVehicleComponent = InWheeledVehicleComponent;
		AnimInstanceProxy.SetWheeledVehicleComponent(InWheeledVehicleComponent);
	}

	const UChaosWheeledVehicleMovementComponent* GetWheeledVehicleComponent() const
	{
		return WheeledVehicleComponent;
	}

private:
	/** UAnimInstance interface begin*/
	virtual void NativeInitializeAnimation() override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
	/** UAnimInstance interface end*/

	FVehicleAnimationInstanceProxy AnimInstanceProxy;

	UPROPERTY(transient)
	const UChaosWheeledVehicleMovementComponent* WheeledVehicleComponent;
};


