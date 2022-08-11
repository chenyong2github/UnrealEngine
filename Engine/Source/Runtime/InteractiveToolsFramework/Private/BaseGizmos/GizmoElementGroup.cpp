// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoElementGroup.h"
#include "BaseGizmos/GizmoViewContext.h"


void UGizmoElementGroup::ApplyUniformConstantScaleToTransform(float PixelToWorldScale, FTransform& InOutLocalToWorldTransform) const
{
	float Scale = InOutLocalToWorldTransform.GetScale3D().X;
	if (bConstantScale)
	{
		Scale *= PixelToWorldScale;
	}
	InOutLocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));
}

void UGizmoElementGroup::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, FVector::ZeroVector, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		ApplyUniformConstantScaleToTransform(CurrentRenderState.PixelToWorldScale, CurrentRenderState.LocalToWorldTransform);

		// Continue render even if not visible so all transforms will be cached 
		// for subsequent line tracing.
		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				Element->Render(RenderAPI, CurrentRenderState);
			}
		}
	}
}

FInputRayHit UGizmoElementGroup::LineTrace(const UGizmoViewContext* ViewContext, const FLineTraceTraversalState& LineTraceState, const FVector& RayOrigin, const FVector& RayDirection)
{
	FInputRayHit Hit;

	FLineTraceTraversalState CurrentLineTraceState(LineTraceState);
	bool bHittableViewDependent = UpdateLineTraceState(ViewContext, FVector::ZeroVector, CurrentLineTraceState);

	if (bHittableViewDependent)
	{
		ApplyUniformConstantScaleToTransform(CurrentLineTraceState.PixelToWorldScale, CurrentLineTraceState.LocalToWorldTransform);

		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				FInputRayHit NewHit = Element->LineTrace(ViewContext, CurrentLineTraceState, RayOrigin, RayDirection);
				if (!Hit.bHit || NewHit.HitDepth < Hit.HitDepth)
				{
					Hit = NewHit;
					if (bHitOwner)
					{
						Hit.SetHitObject(this);
						Hit.HitIdentifier = PartIdentifier;
					}
				}
			}
		}
	}
	return Hit;
}

FBoxSphereBounds UGizmoElementGroup::CalcBounds(const FTransform& LocalToWorld) const
{
	/*  @todo - accumulate box sphere bounds for all elements within the group

	if (bEnabled)
	{
		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				CachedBoxSphereBounds = CachedBoxSphereBounds + Element->CalcBounds(LocalToWorld);
			}
		}
	}
	*/
	return FBoxSphereBounds();
}

void UGizmoElementGroup::Add(UGizmoElementBase* InElement)
{
	if (!Elements.Contains(InElement))
	{
		Elements.Add(InElement);
	}
}

void UGizmoElementGroup::Remove(UGizmoElementBase* InElement)
{
	if (InElement)
	{
		int32 Index;
		if (Elements.Find(InElement, Index))
		{
			Elements.RemoveAtSwap(Index);
		}
	}
}

void UGizmoElementGroup::UpdatePartVisibleState(bool bVisible, uint32 InPartIdentifier)
{
	Super::UpdatePartVisibleState(bVisible, InPartIdentifier);

	for (UGizmoElementBase* Element : Elements)
	{
		if (Element)
		{
			Element->UpdatePartVisibleState(bVisible, InPartIdentifier);
		}
	}
}

void UGizmoElementGroup::UpdatePartHittableState(bool bHittable, uint32 InPartIdentifier)
{
	Super::UpdatePartHittableState(bHittable, InPartIdentifier);

	for (UGizmoElementBase* Element : Elements)
	{
		if (Element)
		{
			Element->UpdatePartHittableState(bHittable, InPartIdentifier);
		}
	}
}

void UGizmoElementGroup::UpdatePartInteractionState(EGizmoElementInteractionState InInteractionState, uint32 InPartIdentifier)
{
	Super::UpdatePartInteractionState(InInteractionState, InPartIdentifier);

	for (UGizmoElementBase* Element : Elements)
	{
		if (Element)
		{
			Element->UpdatePartInteractionState(InInteractionState, InPartIdentifier);
		}
	}
}

void UGizmoElementGroup::SetConstantScale(bool bInConstantScale)
{
	bConstantScale = bInConstantScale;
}

bool UGizmoElementGroup::GetConstantScale() const
{
	return bConstantScale;
}