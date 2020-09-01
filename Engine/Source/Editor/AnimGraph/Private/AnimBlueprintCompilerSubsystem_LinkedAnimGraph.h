// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimBlueprintCompilerSubsystem.h"

#include "AnimBlueprintCompilerSubsystem_LinkedAnimGraph.generated.h"

UCLASS()
class UAnimBlueprintCompilerSubsystem_LinkedAnimGraph : public UAnimBlueprintCompilerSubsystem
{
	GENERATED_BODY()

private:
	// UAnimBlueprintCompilerSubsystem interface
	virtual void PreProcessAnimationNodes(TArrayView<UAnimGraphNode_Base*> InAnimNodes) override;
};