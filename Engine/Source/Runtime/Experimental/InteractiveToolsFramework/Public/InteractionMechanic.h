// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputBehaviorSet.h"
#include "ToolContextInterfaces.h"
#include "InteractiveTool.h"
#include "InteractionMechanic.generated.h"

/**
 * A UInteractionMechanic implements a "user interaction". This is generally a subset of an InteractiveTool,
 * for example an interaction to draw a polygon could be used in many tools, but requires handling input events
 * and converting them to click points on a plane, handling various cases like closing a loop, undo/redo of points, etc.
 * Ideally all these aspects should be able to be wrapped up in an UInteractionMechanic that multiple Tools can use.
 * 
 * (This class is still a work in progress)
 */
UCLASS(Transient)
class INTERACTIVETOOLSFRAMEWORK_API UInteractionMechanic : public UObject
{
	GENERATED_BODY()

public:
	UInteractionMechanic();

	/**
	 * Called to initialize the InteractionMechanic
	 */
	virtual void Setup(UInteractiveTool* ParentTool);

	/**
	 * Called to clean up the InteractionMechanic
	 */
	virtual void Shutdown();

	/**
	 * Allow the Mechanic to do any custom drawing (ie via PDI/RHI)
	 * @param RenderAPI Abstraction that provides access to Rendering in the current ToolsContext
	 */
	virtual void Render(IToolsContextRenderAPI* RenderAPI);

	/**
	 * ALlow the Mechanic to Tick
	 */
	virtual void Tick(float DeltaTime);

protected:
	TWeakObjectPtr<UInteractiveTool> ParentTool;

	UInteractiveTool* GetParentTool() const;

	/**
	 * Add a PropertySet object for this Mechanic to parent Tool
	 * @param PropertySet Property Set object to add
	 */
	virtual void AddToolPropertySource(UInteractiveToolPropertySet* PropertySet);
};

