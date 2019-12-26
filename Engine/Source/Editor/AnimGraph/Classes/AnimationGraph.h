// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraph.h"
#include "Animation/AnimClassInterface.h"
#include "AnimationGraph.generated.h"

class UEdGraphPin;

/** Delegate fired when a pin's default value is changed */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPinDefaultValueChanged, UEdGraphPin* /*InPinThatChanged*/)

UCLASS(MinimalAPI)
class UAnimationGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()

	/** Delegate fired when a pin's default value is changed */
	FOnPinDefaultValueChanged OnPinDefaultValueChanged;

	/** Blending options for animation graphs in Linked Animation Blueprints. */
	UPROPERTY(EditAnywhere, Category = GraphBlending, meta = (ShowOnlyInnerProperties))
	FAnimGraphBlendOptions BlendOptions;
};

