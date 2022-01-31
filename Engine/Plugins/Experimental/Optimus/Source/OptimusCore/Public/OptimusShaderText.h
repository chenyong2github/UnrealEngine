// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OptimusDiagnostic.h"

#include "OptimusShaderText.generated.h"

USTRUCT()
struct FOptimusCompilerDiagnostic
{
	GENERATED_BODY()
	
	FOptimusCompilerDiagnostic() = default;
	FOptimusCompilerDiagnostic(const FOptimusCompilerDiagnostic&) = default;
	FOptimusCompilerDiagnostic& operator=(const FOptimusCompilerDiagnostic&) = default;

	explicit FOptimusCompilerDiagnostic(
			EOptimusDiagnosticLevel InLevel,
			FString InDiagnostic,
			const int32 InLine) :
		Level(InLevel),
		Diagnostic(InDiagnostic),
		Line(InLine)
	{ }

	explicit FOptimusCompilerDiagnostic(
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
	UPROPERTY()
	EOptimusDiagnosticLevel Level = EOptimusDiagnosticLevel::None;

	// The actual diagnostic message
	UPROPERTY()
	FString Diagnostic;
	
	// Line location in source
	UPROPERTY()
	int32 Line = INDEX_NONE;

	// Starting column (inclusive)
	UPROPERTY()
	int32 ColumnStart = INDEX_NONE;

	// Ending column (inclusive)
	UPROPERTY()
	int32 ColumnEnd = INDEX_NONE;
};

USTRUCT()
struct OPTIMUSCORE_API FOptimusShaderText
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	FString Declarations;
	
	UPROPERTY(EditAnywhere, Category=ShaderText)
	FString ShaderText;

	UPROPERTY(VisibleAnywhere, Category=ShaderText)
	TArray<FOptimusCompilerDiagnostic> Diagnostics;
};
