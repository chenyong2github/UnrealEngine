// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "SingleSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API USingleSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	void SetSelection(TUniquePtr<FPrimitiveComponentTarget> ComponentTargetIn)
    {
		ComponentTarget = MoveTemp(ComponentTargetIn);
	}

	/**
	 * @return true if all ComponentTargets of this tool are still valid
	 */
	virtual bool AreAllTargetsValid() const
	{
		return ComponentTarget->IsValid();
	}


public:
	virtual bool CanAccept() const override
	{
		return AreAllTargetsValid();
	}

protected:
	TUniquePtr<FPrimitiveComponentTarget> ComponentTarget{};


};
