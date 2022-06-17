// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementLineBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogGizmoElementLineBase, Log, All);

bool UGizmoElementLineBase::UpdateRenderState(IToolsContextRenderAPI* RenderAPI, const FVector& InLocalOrigin, FRenderTraversalState& InOutRenderState)
{
	InOutRenderState.LineRenderState.Update(LineRenderAttributes);

	return Super::UpdateRenderState(RenderAPI, InLocalOrigin, InOutRenderState);
}

float UGizmoElementLineBase::GetCurrentLineThickness() const
{
	if (ElementInteractionState == EGizmoElementInteractionState::Hovering)
	{
		return (LineThickness > 0.0f ? LineThickness * HoverLineThicknessMultiplier : HoverLineThicknessMultiplier);
	}
	else if (ElementInteractionState == EGizmoElementInteractionState::Interacting)
	{
		return (LineThickness > 0.0f ? LineThickness * InteractLineThicknessMultiplier : InteractLineThicknessMultiplier);
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

void UGizmoElementLineBase::SetLineColor(FLinearColor InLineColor, bool InOverridesChildState)
{
	LineRenderAttributes.LineColor.SetColor(InLineColor, InOverridesChildState);
}

FLinearColor UGizmoElementLineBase::GetLineColor() const
{
	return LineRenderAttributes.LineColor.GetColor();
}

bool UGizmoElementLineBase::HasLineColor() const
{
	return LineRenderAttributes.LineColor.bHasValue;
}

bool UGizmoElementLineBase::GetLineColorOverridesChildState() const
{
	return LineRenderAttributes.LineColor.bOverridesChildState;
}

void UGizmoElementLineBase::ClearLineColor()
{
	LineRenderAttributes.LineColor.Reset();
}

void UGizmoElementLineBase::SetHoverLineColor(FLinearColor InHoverLineColor, bool InOverridesChildState)
{
	LineRenderAttributes.HoverLineColor.SetColor(InHoverLineColor, InOverridesChildState);
}

FLinearColor UGizmoElementLineBase::GetHoverLineColor() const
{
	return LineRenderAttributes.HoverLineColor.GetColor();
}
bool UGizmoElementLineBase::HasHoverLineColor() const
{
	return LineRenderAttributes.HoverLineColor.bHasValue;
}

bool UGizmoElementLineBase::GetHoverLineColorOverridesChildState() const
{
	return LineRenderAttributes.HoverLineColor.bOverridesChildState;
}

void UGizmoElementLineBase::ClearHoverLineColor()
{
	LineRenderAttributes.HoverLineColor.Reset();
}

void UGizmoElementLineBase::SetInteractLineColor(FLinearColor InInteractLineColor, bool InOverridesChildState)
{
	LineRenderAttributes.InteractLineColor.SetColor(InInteractLineColor, InOverridesChildState);
}

FLinearColor UGizmoElementLineBase::GetInteractLineColor() const
{
	return LineRenderAttributes.InteractLineColor.GetColor();
}
bool UGizmoElementLineBase::HasInteractLineColor() const
{
	return LineRenderAttributes.InteractLineColor.bHasValue;
}

bool UGizmoElementLineBase::GetInteractLineColorOverridesChildState() const
{
	return LineRenderAttributes.InteractLineColor.bOverridesChildState;
}

void UGizmoElementLineBase::ClearInteractLineColor()
{
	LineRenderAttributes.InteractLineColor.Reset();
}



