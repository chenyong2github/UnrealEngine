// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InteractiveTool.h"
#include "ComponentSourceInterfaces.h"
#include "ToolTargets/ToolTarget.h"

#include "SingleSelectionTool.generated.h"

UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API USingleSelectionTool : public UInteractiveTool
{
GENERATED_BODY()
public:
	// @deprecated Use SetTarget instead, and don't use FPrimitiveComponentTarget.
	void SetSelection(TUniquePtr<FPrimitiveComponentTarget> ComponentTargetIn)
    {
		ComponentTarget = MoveTemp(ComponentTargetIn);
	}

	void SetTarget(UToolTarget* TargetIn)
	{
		Target = TargetIn;
	}

	/**
	 * @return true if all ComponentTargets of this tool are still valid
	 */
	virtual bool AreAllTargetsValid() const
	{
		// TODO: This needs to be updated once tools no longer use ComponentTarget.
		return Target ? Target->IsValid() : ComponentTarget->IsValid();
	}


public:
	virtual bool CanAccept() const override
	{
		return AreAllTargetsValid();
	}

protected:
	/** @deprecated Tools should use Target instead. */
	TUniquePtr<FPrimitiveComponentTarget> ComponentTarget{};

	UPROPERTY()
	TObjectPtr<UToolTarget> Target;
};
