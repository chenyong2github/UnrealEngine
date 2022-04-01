// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MLDeformerCurveReference.generated.h"

USTRUCT()
struct MLDEFORMERFRAMEWORK_API FMLDeformerCurveReference
{
	GENERATED_USTRUCT_BODY()

	FMLDeformerCurveReference(const FName& InCurveName=NAME_None)
		: CurveName(InCurveName)
	{
	}

	bool operator==(const FMLDeformerCurveReference& Other) const
	{
		return (CurveName == Other.CurveName);
	}

	bool operator!=(const FMLDeformerCurveReference& Other) const
	{
		return (CurveName != Other.CurveName);
	}

	friend FArchive& operator<<(FArchive& Ar, FMLDeformerCurveReference& C)
	{
		Ar << C.CurveName;
		return Ar;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	/** Name of curve. */
	UPROPERTY(EditAnywhere, Category = AnimCurveReference)
	FName CurveName;
};
