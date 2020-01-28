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
		// find view to use for gizmo sizing/etc
		const FSceneView* GizmoControlView = GizmoRenderingUtil::FindActiveSceneView(Views, ViewFamily, VisibilityMap);
		if (GizmoControlView == nullptr)
		{
			return;
		}

		FVector LocalOffset = Direction;
		if (ExternalDistance != nullptr)
		{
			LocalOffset *= (*ExternalDistance);
		}

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();

		float IntervalMarkerSize = HandleSize;
		FVector WorldIntervalEnd = LocalToWorldMatrix.TransformPosition(LocalOffset + IntervalMarkerSize * Normal);

		FVector WorldDiskOrigin = LocalToWorldMatrix.TransformPosition(LocalOffset);
		FVector WorldBaseOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);


		float LengthScale = 1.0f;
		if (ExternalDynamicPixelToWorldScale != nullptr && bImageScale)
		{
			float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(GizmoControlView, WorldDiskOrigin);
			*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
			LengthScale = PixelToWorldScale;
		}


		FVector ScaledDiskOrigin = LengthScale * (WorldDiskOrigin - WorldBaseOrigin) + WorldBaseOrigin;
		FVector ScaledIntevalStart = -LengthScale * (WorldIntervalEnd - WorldDiskOrigin) + WorldDiskOrigin;
		FVector ScaledIntevalEnd = LengthScale * (WorldIntervalEnd - WorldDiskOrigin) + WorldDiskOrigin;


		//double UseRadius = LengthScale * Radius;

		FLinearColor BackColor = FLinearColor(0.5f, 0.5f, 0.5f);
		float BackThickness = 0.5f;

		float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
			(HoverThicknessMultiplier*Thickness) : (Thickness);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				FVector UpVector = View->GetViewUp();


				FVector ViewVector = View->GetViewDirection();

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

	float* ExternalDistance = nullptr;
	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;
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