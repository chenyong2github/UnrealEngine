// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "CurveReference.generated.h"

USTRUCT()
struct FCurveReference
{
	GENERATED_USTRUCT_BODY()

	FCurveReference(const FName& InCurveName=NAME_None)
		: CurveName(InCurveName)
	{
	}

	bool operator==(const FCurveReference& Other) const
	{
		return (CurveName == Other.CurveName);
	}

	bool operator!=(const FCurveReference& Other) const
	{
		return (CurveName != Other.CurveName);
	}

	friend FArchive& operator<<(FArchive& Ar, FCurveReference& C)
	{
		Ar << C.CurveName;
		return Ar;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}

	/** Name of curve. **/
	UPROPERTY(EditAnywhere, Category = AnimCurveReference)
	FName CurveName;
};
