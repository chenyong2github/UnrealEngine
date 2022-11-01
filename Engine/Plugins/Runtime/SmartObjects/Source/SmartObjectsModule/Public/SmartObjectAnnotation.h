// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SmartObjectTypes.h"
#include "SmartObjectAnnotation.generated.h"

struct FSmartObjectVisualizationContext;

/**
 * Base class for Smart Object Slot annotations. Annotation is a specific type of slot definition data that has methods to visualize it.
 */
USTRUCT(meta=(Hidden))
struct SMARTOBJECTSMODULE_API FSmartObjectSlotAnnotation : public FSmartObjectSlotDefinitionData
{
	GENERATED_BODY()
	virtual ~FSmartObjectSlotAnnotation() override {}

#if UE_ENABLE_DEBUG_DRAWING
	// @todo: Try to find a way to add visualization without requiring virtual functions.
	/** Methods to override to draw 3D visualization of the annotation. */
	virtual void DrawVisualization(FSmartObjectVisualizationContext& VisContext) const {}
	/** Methods to override to draw canvas visualization of the annotation. */
	virtual void DrawVisualizationHUD(FSmartObjectVisualizationContext& VisContext) const {}
#endif
};
