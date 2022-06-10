// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeFramework/ComputeSource.h"
#include "OptimusSource.generated.h"

UCLASS()
class OPTIMUSCORE_API UOptimusSource 
	: public UComputeSource
{
	GENERATED_BODY()

public:
	// Begin UComputeSource interface.
	FString GetSource() const override { return SourceText; }
	// End UComputeSource interface.

	void SetSource(const FString& InText);

protected:
	/** HLSL Source. */
	UPROPERTY(EditAnywhere, Category = Source, meta = (DisplayAfter = "AdditionalSources"))
	FString SourceText;
};
