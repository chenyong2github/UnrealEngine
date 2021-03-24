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
	void SetTarget(UToolTarget* TargetIn)
	{
		Target = TargetIn;
	}

	/**
	 * @return true if all ToolTargets of this tool are still valid
	 */
	virtual bool AreAllTargetsValid() const
	{
		return Target ? Target->IsValid() : false;
	}


public:
	virtual bool CanAccept() const override
	{
		return AreAllTargetsValid();
	}

protected:
	UPROPERTY()
	TObjectPtr<UToolTarget> Target;
};
