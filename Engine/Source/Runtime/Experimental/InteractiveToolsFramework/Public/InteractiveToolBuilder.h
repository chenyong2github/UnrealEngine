// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.generated.h"



/**
 * A UInteractiveToolBuilder creates a new instance of an InteractiveTool (basically this is a Factory).
 * These are registered with the InteractiveToolManager, which calls BuildTool() if CanBuildTool() returns true.
 * In addition CanBuildTool() will be queried to (for example) enable/disable UI buttons, etc.
 * This is an abstract base class, you must subclass it in order to create your particular Tool instance
 */
UCLASS(Transient, Abstract)
class INTERACTIVETOOLSFRAMEWORK_API UInteractiveToolBuilder : public UObject
{
	GENERATED_BODY()

public:

	/** 
	 * Check if, given the current scene state, a new instance of this builder's Tool can be created
	 * @param SceneState the current scene selection state, etc
	 * @return true if a new Tool instance can be created
	 */
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const
	{
		check(false);
		return false;
	}

	/** 
	 * Create a new instance of this builder's Tool
	 * @param SceneState the current scene selection state, etc
	 * @return a new instance of the Tool, or nullptr on error/failure
	 */
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const
	{
		check(false);
		return nullptr;
	}
};