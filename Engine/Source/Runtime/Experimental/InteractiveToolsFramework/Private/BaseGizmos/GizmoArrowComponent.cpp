// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoArrowComponent.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "PrimitiveSceneProxy.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "SceneManagement.h"


class FGizmoArrowComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGizmoArrowComponentSceneProxy(const UGizmoArrowComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent),
		Color(InComponent->Color),
		Direction(InComponent->Direction),
		Gap(InComponent->Gap),
		Length(InComponent->Length),
		Thickness(InComponent->Thickness),
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

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		// direction to origin of gizmo
		FVector ViewDirection = Origin - GizmoControlView->ViewLocation;
		ViewDirection.Normalize();

		bool bWorldAxis = (bExternalWorldLocalState) ? (*bExternalWorldLocalState) : false;
		FVector ArrowDirection = (bWorldAxis) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };
		bool bFlipped = (FVector::DotProduct(ViewDirection, ArrowDirection) > 0);
		ArrowDirection = (bFlipped) ? -ArrowDirection : ArrowDirection;
		if (bFlippedExternal != nullptr)
		{
			*bFlippedExternal = bFlipped;
		}

		if (bExternalRenderVisibility != nullptr)
		{
			//*bExternalRenderVisibility = FMath::Abs(FVector::DotProduct(ArrowDirection, ViewDirection)) < 0.985f;   // ~10 degrees
			*bExternalRenderVisibility = FMath::Abs(FVector::DotProduct(ArrowDirection, ViewDirection)) < 0.965f;   // ~15 degrees
			if (*bExternalRenderVisibility == false)
			{
				return;
			}
		}

		float LengthScale = 1.0f;
		if (ExternalDynamicPixelToWorldScale != nullptr)
		{
			float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(GizmoControlView, Origin);
			*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
			LengthScale = PixelToWorldScale;
		}

		float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
			(HoverThicknessMultiplier*Thickness) : (Thickness);

		double StartDist = LengthScale * Gap;
		double EndDist = LengthScale * (Gap+Length);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				FVector StartPoint = Origin + StartDist*ArrowDirection;
				FVector EndPoint = Origin + EndDist*ArrowDirection;

				PDI->DrawLine(StartPoint, EndPoint, Color, SDPG_Foreground, UseThickness, 0.0f, true);
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


	void SetExternalFlip(bool* bFlipped)
	{
		bFlippedExternal = bFlipped;
	}

	void SetExternalDynamicPixelToWorldScale(float* DynamicPixelToWorldScale)
	{
		ExternalDynamicPixelToWorldScale = DynamicPixelToWorldScale;
	}

	void SetExternalRenderVisibility(bool* bRenderVisibility)
	{
		bExternalRenderVisibility = bRenderVisibility;
	}

	void SetExternalHoverState(bool* HoverState)
	{
		bExternalHoverState = HoverState;
	}

	void SetExternalWorldLocalState(bool* bWorldLocalState)
	{
		bExternalWorldLocalState = bWorldLocalState;
	}

private:
	FLinearColor Color;
	FVector Direction;
	float Gap;
	float Length;
	float Thickness;
	float HoverThicknessMultiplier;

	bool* bFlippedExternal = nullptr;
	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalRenderVisibility = nullptr;
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;
};


FPrimitiveSceneProxy* UGizmoArrowComponent::CreateSceneProxy()
{
	FGizmoArrowComponentSceneProxy* NewProxy = new FGizmoArrowComponentSceneProxy(this);
	NewProxy->SetExternalFlip(&bFlipped);
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
	NewProxy->SetExternalRenderVisibility(&bRenderVisibility);
	NewProxy->SetExternalHoverState(&bHovering);
	NewProxy->SetExternalWorldLocalState(&bWorld);
	return NewProxy;
}

FBoxSphereBounds UGizmoArrowComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FSphere(FVector::ZeroVector, Gap+Length).TransformBy(LocalToWorld));
}

bool UGizmoArrowComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	if (bRenderVisibility == false)
	{
		return false;
	}

	const FTransform& Transform = this->GetComponentToWorld();

	FVector UseDirection = (bFlipped) ? -Direction : Direction;
	float LengthScale = DynamicPixelToWorldScale;
	double StartDist = LengthScale * Gap;
	double EndDist = LengthScale * (Gap + Length);

	UseDirection = (bWorld) ? UseDirection : Transform.TransformVector(UseDirection);
	FVector UseOrigin = Transform.TransformPosition(FVector::ZeroVector);
	FVector Point0 = UseOrigin + StartDist * UseDirection;
	FVector Point1 = UseOrigin + EndDist * UseDirection;

	FVector NearestArrow, NearestLine;
	FMath::SegmentDistToSegmentSafe(Point0, Point1, Start, End, NearestArrow, NearestLine);
	double Distance = FVector::Distance(NearestArrow, NearestLine);
	if (Distance > PixelHitDistanceThreshold*DynamicPixelToWorldScale)
	{
		return false;
	}

	OutHit.Component = this;
	OutHit.Distance = FVector::Distance(Start, NearestLine);
	OutHit.ImpactPoint = NearestLine;
	return true;
}