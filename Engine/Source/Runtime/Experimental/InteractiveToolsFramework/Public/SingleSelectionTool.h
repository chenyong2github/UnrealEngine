// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "SingleSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API USingleSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	void SetSelection(FComponentTarget ComponentTargetIn)
    {
		ComponentTarget = MoveTemp(ComponentTargetIn);
	}
protected:
	FComponentTarget ComponentTarget{};
};
