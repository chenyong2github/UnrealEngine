// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "OptimusType_ShaderText.generated.h"

USTRUCT()
struct FOptimusSourceLocation
{
	GENERATED_BODY()
	
	FOptimusSourceLocation() = default;
	FOptimusSourceLocation(const FOptimusSourceLocation&) = default;
	FOptimusSourceLocation& operator=(const FOptimusSourceLocation&) = default;

	explicit FOptimusSourceLocation(const int32 InLine) : Line(InLine) {}

	explicit FOptimusSourceLocation(const int32 InLine, const int32 InColumnStart, const int32 InColumnEnd) :
        Line(InLine), ColumnStart(InColumnStart), ColumnEnd(InColumnEnd) {}
	
	// Line location in source
	int32 Line = INDEX_NONE;

	// Starting column (inclusive)
	int32 ColumnStart = INDEX_NONE;

	// Ending column (inclusive)
	int32 ColumnEnd = INDEX_NONE;
};

USTRUCT()
struct OPTIMUSDEVELOPER_API FOptimusType_ShaderText
{
	GENERATED_BODY()

	FString GetSource() const
	{
		return ShaderPreamble + TEXT("\n#line 1") + ShaderText + TEXT("\n") + ShaderEpilogue;
	}
	
	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	FString ShaderPreamble;

	UPROPERTY(EditAnywhere, Category=ShaderText)
	FString ShaderText;

	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	FString ShaderEpilogue;
};
