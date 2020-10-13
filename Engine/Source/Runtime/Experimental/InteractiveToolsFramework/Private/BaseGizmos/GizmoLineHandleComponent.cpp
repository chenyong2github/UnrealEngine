// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoLineHandleComponent.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "PrimitiveSceneProxy.h"




class FGizmoLineHandleComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGizmoLineHandleComponentSceneProxy(const UGizmoLineHandleComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent),
		Color(InComponent->Color),
		Normal(InComponent->Normal),
		Direction(InComponent->Direction),
		HandleSize(InComponent->HandleSize),
		Thickness(InComponent->Thickness),
		bBoundaryOnly(false),
		bImageScale(InComponent->bImageScale),
		HoverThicknessMultiplier(InComponent->HoverSizeMultiplier)
	{
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		// try to find focused scene view. May return nullptr.
		const FSceneView* FocusedView = GizmoRenderingUtil::FindFocusedEditorSceneView(Views, ViewFamily, VisibilityMap);

		FVector LocalOffset = Direction;
		if (ExternalDistance != nullptr)
		{
			LocalOffset *= (*ExternalDistance);
		}

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector WorldOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		float IntervalMarkerSize = HandleSize;
		FVector WorldIntervalEnd = LocalToWorldMatrix.TransformPosition(LocalOffset + IntervalMarkerSize * Normal);
		FVector WorldDiskOrigin = LocalToWorldMatrix.TransformPosition(LocalOffset);
		FVector WorldBaseOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				bool bIsFocusedView = (FocusedView != nullptr && View == FocusedView);
				bool bIsOrtho = !View->IsPerspectiveProjection();
				FVector UpVector = View->GetViewUp();
				FVector ViewVector = View->GetViewDirection();

				float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(View, WorldDiskOrigin);
				float LengthScale = PixelToWorldScale;
				if (bIsFocusedView && ExternalDynamicPixelToWorldScale != nullptr)
				{
					*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
				}

				FVector ScaledIntevalStart = -LengthScale * (WorldIntervalEnd - WorldDiskOrigin) + WorldDiskOrigin;
				FVector ScaledIntevalEnd = LengthScale * (WorldIntervalEnd - WorldDiskOrigin) + WorldDiskOrigin;
				FVector ScaledDiskOrigin = 0.5 * (ScaledIntevalStart + ScaledIntevalEnd);

				float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
					(HoverThicknessMultiplier * Thickness) : (Thickness);
				if (!bIsOrtho)
				{
					UseThickness *= (View->FOV / 90.0);		// compensate for FOV scaling in Gizmos...
				}

				// From base origin to disk origin
				PDI->DrawLine(WorldBaseOrigin, ScaledDiskOrigin, Color, SDPG_Foreground, UseThickness, 0.0f, true);
				// Draw the interval marker
				PDI->DrawLine(ScaledIntevalStart, ScaledIntevalEnd, Color, SDPG_Foreground, 2. * UseThickness, 0.0f, true);
			}
		}
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		Result.bShadowRelevance = false;
		Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
		Result.bRenderCustomDepth = ShouldRenderCustomDepth();
		return Result;
	}

	virtual bool CanBeOccluded() const override
	{
		return false;
	}

	virtual uint32 GetMemoryFootprint(void) const override { return sizeof *this + GetAllocatedSize(); }
	uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }


	void SetExternalDynamicPixelToWorldScale(float* DynamicPixelToWorldScale)
	{
		ExternalDynamicPixelToWorldScale = DynamicPixelToWorldScale;
	}

	void SetExternalHoverState(bool* HoverState)
	{
		bExternalHoverState = HoverState;
	}

	void SetExternalWorldLocalState(bool* bWorldLocalState)
	{
		bExternalWorldLocalState = bWorldLocalState;
	}

	void SetLengthScale(float* Distance)
	{
		ExternalDistance = Distance;
	}

private:
	FLinearColor Color;
	FVector Normal;
	FVector Direction;
	float HandleSize;
	float Thickness;
	bool bBoundaryOnly;
	bool bImageScale;
	float HoverThicknessMultiplier;

	// set on Component for use in ::GetDynamicMeshElements()
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;
	float* ExternalDistance = nullptr;

	// set in ::GetDynamicMeshElements() for use by Component hit testing
	float* ExternalDynamicPixelToWorldScale = nullptr;
};


FPrimitiveSceneProxy* UGizmoLineHandleComponent::CreateSceneProxy()
{
	FGizmoLineHandleComponentSceneProxy* NewProxy = new FGizmoLineHandleComponentSceneProxy(this);
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
	NewProxy->SetExternalHoverState(&bHovering);
	NewProxy->SetExternalWorldLocalState(&bWorld);
	NewProxy->SetLengthScale(&Length);
	return NewProxy;
}

FBoxSphereBounds UGizmoLineHandleComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// the handle looks like
	//                         ------|
	// where '------' has length "Length" and '|' is of length 2*Handlesize
	//  
	float Radius = FMath::Sqrt(Length * Length + HandleSize * HandleSize);
	return FBoxSphereBounds(FSphere(FVector::ZeroVector, Radius).TransformBy(LocalToWorld));
}

bool UGizmoLineHandleComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	float LengthScale = (bImageScale) ? DynamicPixelToWorldScale : 1.f;
	double UseHandleSize = LengthScale * HandleSize;
	FVector LocalOffset = Length * Direction;

	const FTransform& Transform = this->GetComponentToWorld();
	FVector HandleDir = (bWorld) ? Normal : Transform.TransformVector(Normal);
	FVector WorldBaseOrigin = Transform.TransformPosition(FVector::ZeroVector);
	FVector WorldHandleOrigin = Transform.TransformPosition(LocalOffset);

	FVector BaseToHandle = WorldHandleOrigin - WorldBaseOrigin;

	// where the handle crosses the connecting line
	FVector ScaledHandleOrigin = LengthScale * BaseToHandle + WorldBaseOrigin;

	// start and end point of the handle.
	FVector HandleStart = ScaledHandleOrigin + LengthScale * HandleDir;
	FVector HandleEnd = ScaledHandleOrigin - LengthScale * HandleDir;

	FVector NearestOnHandle, NearestOnLine;
	FMath::SegmentDistToSegmentSafe(HandleStart, HandleEnd, Start, End, NearestOnHandle, NearestOnLine);
	double Distance = FVector::Distance(NearestOnHandle, NearestOnLine);
	if (Distance > PixelHitDistanceThreshold*DynamicPixelToWorldScale)
	{
		return false;
	}

	OutHit.Component = this;
	OutHit.Distance = FVector::Distance(Start, NearestOnLine);
	OutHit.ImpactPoint = NearestOnLine;
	return true;

}