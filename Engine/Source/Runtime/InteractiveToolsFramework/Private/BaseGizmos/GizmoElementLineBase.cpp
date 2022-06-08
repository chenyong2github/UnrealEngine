// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementLineBase.h"
#include "BaseGizmos/GizmoElementBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogGizmoElementLineBase, Log, All);

float UGizmoElementLineBase::GetCurrentLineThickness() const
{
	if (ElementInteractionState == EGizmoElementInteractionState::Hovering)
	{
		return (LineThickness == 0.0f ? HoverLineThicknessMultiplier : LineThickness * HoverLineThicknessMultiplier);
	}
	else if (ElementInteractionState == EGizmoElementInteractionState::Interacting)
	{
		return (LineThickness == 0.0f ? InteractLineThicknessMultiplier : LineThickness * InteractLineThicknessMultiplier);
	}

	return LineThickness;
}

void UGizmoElementLineBase::SetLineThickness(float InLineThickness)
{
	if (InLineThickness < 0.0)
	{
		UE_LOG(LogGizmoElementLineBase, Warning, TEXT("Invalid gizmo element line thickness %f, will be set to 0.0."), InLineThickness);
		LineThickness = 0.0;
	}
	else
	{
		LineThickness = InLineThickness;
	}
}

float UGizmoElementLineBase::GetLineThickness() const
{
	return LineThickness;
}

void UGizmoElementLineBase::SetHoverLineThicknessMultiplier(float InHoverLineThicknessMultiplier)
{
	HoverLineThicknessMultiplier = InHoverLineThicknessMultiplier;
}

float UGizmoElementLineBase::GetHoverLineThicknessMultiplier() const
{
	return HoverLineThicknessMultiplier;
}

void UGizmoElementLineBase::SetInteractLineThicknessMultiplier(float InInteractLineThicknessMultiplier)
{
	InteractLineThicknessMultiplier = InInteractLineThicknessMultiplier;
}

float UGizmoElementLineBase::GetInteractLineThicknessMultiplier() const
{
	return InteractLineThicknessMultiplier;
}

