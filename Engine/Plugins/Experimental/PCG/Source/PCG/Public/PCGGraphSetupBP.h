// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGGraphSetupBP.generated.h"

class UPCGGraph;

UCLASS(Abstract, BlueprintType, Blueprintable, hidecategories=(Object))
class UPCGGraphSetupBP : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = Graph)
	void Setup(UPCGGraph* Graph);
};