// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoElementGroup.h"

void UGizmoElementGroup::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	check(RenderAPI);

	FRenderTraversalState CurrentRenderState(RenderState);
	bool bVisibleViewDependent = UpdateRenderState(RenderAPI, FVector::ZeroVector, CurrentRenderState);

	if (bVisibleViewDependent)
	{
		// Compute constant scale, if applicable
		float Scale = CurrentRenderState.LocalToWorldTransform.GetScale3D().X;
		if (bConstantScale)
		{
			Scale *= CurrentRenderState.PixelToWorldScale;
		}
		CurrentRenderState.LocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));

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

FInputRayHit UGizmoElementGroup::LineTrace(const FVector Start, const FVector Direction)
{
	FInputRayHit Hit;

	if (IsHittable())
	{
		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				FInputRayHit NewHit = Element->LineTrace(Start, Direction);
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

void UGizmoElementGroup::ResetCachedRenderState()
{
	Super::ResetCachedRenderState();

	for (UGizmoElementBase* Element : Elements)
	{
		if (Element)
		{
			Element->ResetCachedRenderState();
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