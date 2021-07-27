// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OptimusDiagnostic.h"

#include "OptimusType_ShaderText.generated.h"

USTRUCT()
struct FOptimusType_CompilerDiagnostic
{
	GENERATED_BODY()
	
	FOptimusType_CompilerDiagnostic() = default;
	FOptimusType_CompilerDiagnostic(const FOptimusType_CompilerDiagnostic&) = default;
	FOptimusType_CompilerDiagnostic& operator=(const FOptimusType_CompilerDiagnostic&) = default;

	explicit FOptimusType_CompilerDiagnostic(
			EOptimusDiagnosticLevel InLevel,
			FString InDiagnostic,
			const int32 InLine) :
		Level(InLevel),
		Diagnostic(InDiagnostic),
		Line(InLine)
	{ }

	explicit FOptimusType_CompilerDiagnostic(
			EOptimusDiagnosticLevel InLevel,
			FString InDiagnostic,
			const int32 InLine,
			const int32 InColumnStart,
			const int32 InColumnEnd) :
		Level(InLevel),
		Diagnostic(InDiagnostic),
		Line(InLine), ColumnStart(InColumnStart), ColumnEnd(InColumnEnd)
	{ }

	// The severity of the issue.
	EOptimusDiagnosticLevel Level = EOptimusDiagnosticLevel::Ignore;

	// The actual diagnostic message
	FString Diagnostic;
	
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

	// 
	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	FString Declarations;
	
	UPROPERTY(EditAnywhere, Category=ShaderText)
	FString ShaderText;

	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	TArray<FOptimusType_CompilerDiagnostic> Diagnostics;
};
