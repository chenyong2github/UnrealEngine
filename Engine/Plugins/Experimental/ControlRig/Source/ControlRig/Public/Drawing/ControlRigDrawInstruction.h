// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigDrawInstruction.generated.h"

UENUM()
namespace EControlRigDrawSettings
{
	enum Primitive
	{
		Points,
		Lines,
		LineStrip
	};
}

USTRUCT()
struct CONTROLRIG_API FControlRigDrawInstruction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	FName Name;

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	TEnumAsByte<EControlRigDrawSettings::Primitive> PrimitiveType;

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	TArray<FVector> Positions;

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	FLinearColor Color;

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	float Thickness;

	UPROPERTY(EditAnywhere, Category = "DrawInstruction")
	FTransform Transform;

	FControlRigDrawInstruction()
		: Name(NAME_None)
		, PrimitiveType(EControlRigDrawSettings::Points)
		, Color(FLinearColor::Red)
		, Thickness(0.f)
		, Transform(FTransform::Identity)
	{}

	FControlRigDrawInstruction(EControlRigDrawSettings::Primitive InPrimitiveType, const FLinearColor& InColor, float InThickness = 0.f, FTransform InTransform = FTransform::Identity)
		: Name(NAME_None)
		, PrimitiveType(InPrimitiveType)
		, Color(InColor)
		, Thickness(InThickness)
		, Transform(InTransform)
	{}
};
