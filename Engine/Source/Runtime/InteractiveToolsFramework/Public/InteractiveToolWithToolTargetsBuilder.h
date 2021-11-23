// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "ToolTargets/ToolTarget.h"

#include "InteractiveToolWithToolTargetsBuilder.generated.h"



/**
 * A UInteractiveToolWithToolTargetsBuilder creates a new instance of an InteractiveTool that uses tool targets.
 * See ToolTarget.h for more information on tool targets and their usage. This class defines the common
 * interface(s) for defining the tool target requirements of the tool it builds.
 * This is an abstract base class, you must subclass it in order to create your particular Tool instance.
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolWithToolTargetsBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

protected:
	/**
	 * Gives the target requirements of the associated tool. Usually, it is the tool builder
	 * will use this function in CanBuildTool and BuildTool to find and create any necessary targets.
	 */
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const
	{
		static FToolTargetTypeRequirements TypeRequirements; // Default initialized to no requirements.
		return TypeRequirements;
	}
};
