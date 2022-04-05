// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Axis.h"

class FSceneView;
class FPrimitiveDrawInterface;

/** Widget used to manipulate entities in the light card editor's viewport */
class FDisplayClusterLightCardEditorWidget
{
public:
	constexpr static float AxisLength = 100.f;
	constexpr static float AxisThickness = 35.f;
	constexpr static float OriginSize = 200.f;
	constexpr static FLinearColor AxisColorX = FLinearColor(0.594f, 0.0197f, 0.0f);
	constexpr static FLinearColor AxisColorY = FLinearColor(0.1349f, 0.3959f, 0.0f);
	constexpr static FLinearColor AxisColorZ = FLinearColor(0.0251f, 0.207f, 0.85f);
	constexpr static FLinearColor HighlightColor = FLinearColor(1.0f, 1.0f, 0.0f);

	void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI);

	void SetTransform(const FTransform& NewTransform) { Transform = FTransform(NewTransform); }
	void SetHighlightedAxis(EAxisList::Type InAxis) { HighlightedAxis = InAxis; }
	void SetWidgetScale(float InWidgetScale) { WidgetScale = InWidgetScale; }

private:
	void DrawAxis(const FSceneView* View, FPrimitiveDrawInterface* PDI, EAxisList::Type Axis);
	void DrawOrigin(const FSceneView* View, FPrimitiveDrawInterface* PDI);

	FVector GetGlobalAxis(EAxisList::Type Axis) const;
	FLinearColor GetAxisColor(EAxisList::Type Axis) const;

private:
	/** The world transform to apply to the widget when rendering */
	FTransform Transform;

	/** The axis on the widget which should be highlighted */
	EAxisList::Type HighlightedAxis = EAxisList::Type::None;

	/** A scale factor to scale the widget's rendered size */
	float WidgetScale = 1.f;
};