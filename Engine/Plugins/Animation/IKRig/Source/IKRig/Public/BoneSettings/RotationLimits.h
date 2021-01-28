// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IKRigBoneSetting.h"
#include "RotationLimits.generated.h"

UENUM(BlueprintType)
enum class ERotationLimitMode : uint8
{
	Free	UMETA(DisplayName = "Free"),
	Limited	UMETA(DisplayName = "Limited"),
	Locked	UMETA(DisplayName = "Locked"),
};

UCLASS(EditInlineNew, config = Engine, hidecategories = UObject, BlueprintType)
class IKRIG_API URotationLimits : public UIKRigBoneSetting
{
	GENERATED_BODY()

	URotationLimits() {}

	UPROPERTY(EditAnywhere, Category = "Limit")
	ERotationLimitMode RotateX;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MinX = -10.0f;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MaxX = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Limit")
	ERotationLimitMode RotateY;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MinY = -10.0f;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MaxY = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Limit")
	ERotationLimitMode RotateZ;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MinZ = -10.0f;
	UPROPERTY(EditAnywhere, Category = "Limit")
	float MaxZ = 10.0f;
};

