// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Interface.h"

#include "IOptimusShaderTextProvider.generated.h"

struct FOptimusShaderText;
struct FOptimusCompilerDiagnostic;

UINTERFACE()
class OPTIMUSCORE_API UOptimusShaderTextProvider :
	public UInterface
{
	GENERATED_BODY()
};

/**
* Interface that provides a mechanism to retrieve shader text.
*/
class OPTIMUSCORE_API IOptimusShaderTextProvider
{
	GENERATED_BODY()

public:


	virtual FString GetNameForShaderTextEditor() const = 0;

	virtual FString GetDeclarations() const = 0;

	virtual FString GetShaderText() const = 0;

	virtual void SetShaderText(const FString& NewText) = 0;

	virtual const TArray<FOptimusCompilerDiagnostic>& GetCompilationDiagnostics() const = 0;
	
#if WITH_EDITOR	
	DECLARE_EVENT(IOptimusShaderTextProvider, FOnDiagnosticsUpdated);
	virtual FOnDiagnosticsUpdated& OnDiagnosticsUpdated() = 0;
#endif
};
