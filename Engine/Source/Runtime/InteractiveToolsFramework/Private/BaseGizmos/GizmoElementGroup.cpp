// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoElementGroup.h"
#include "BaseGizmos/GizmoRenderingUtil.h"


void UGizmoElementGroup::Render(IToolsContextRenderAPI* RenderAPI, const FRenderTraversalState& RenderState)
{
	if (!IsVisible())
	{
		return;
	}

	const FSceneView* View = RenderAPI->GetSceneView();

	bool bVisibleViewDependent = GetViewDependentVisibility(View, RenderState.LocalToWorldTransform, FVector::ZeroVector);

	FRenderTraversalState RenderStateCopy = RenderState;

	if (bVisibleViewDependent)
	{
		// Compute constant scale, if applicable
		float Scale = RenderStateCopy.LocalToWorldTransform.GetScale3D().X;
		if (bConstantScale)
		{
			const float ConstantScaleFactor = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(RenderAPI->GetSceneView(), RenderStateCopy.LocalToWorldTransform.GetTranslation());
			Scale *= ConstantScaleFactor;
		}
		RenderStateCopy.LocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));

		// Compute view alignment, if applicable
		FQuat AlignRot;
		if (GetViewAlignRot(View, RenderStateCopy.LocalToWorldTransform, FVector::ZeroVector, AlignRot))
		{
			RenderStateCopy.LocalToWorldTransform = FTransform(AlignRot) * RenderState.LocalToWorldTransform;
		}

		UpdateRenderTraversalState(RenderStateCopy);

		// Continue render even if not visible so all transforms will be cached 
		// for subsequent line tracing.
		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				Element->Render(RenderAPI, RenderStateCopy);
			}
		}
	}

	CacheRenderState(RenderStateCopy.LocalToWorldTransform, bVisibleViewDependent);
}

FInputRayHit UGizmoElementGroup::LineTrace(const FVector Start, const FVector Direction)
{
	if (IsHittable())
	{
		for (UGizmoElementBase* Element : Elements)
		{
			if (Element)
			{
				return Element->LineTrace(Start, Direction);
			}
		}
	}
	return FInputRayHit();
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