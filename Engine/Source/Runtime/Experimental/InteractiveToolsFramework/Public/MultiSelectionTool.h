// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "MultiSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UMultiSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	void SetSelection(TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargetsIn)
    {
		ComponentTargets = MoveTemp(ComponentTargetsIn);
	}
protected:
	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets{};
};
