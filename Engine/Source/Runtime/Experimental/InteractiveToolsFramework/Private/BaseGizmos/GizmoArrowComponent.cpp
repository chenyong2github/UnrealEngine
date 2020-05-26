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
		// try to find focused scene view. May return nullptr.
		const FSceneView* FocusedView = GizmoRenderingUtil::FindFocusedEditorSceneView(Views, ViewFamily, VisibilityMap);

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				bool bIsFocusedView = (FocusedView != nullptr && View == FocusedView);
				bool bIsOrtho = !View->IsPerspectiveProjection();

				// direction to origin of gizmo
				FVector ViewDirection = 
					(bIsOrtho) ?  (View->GetViewDirection()) : (Origin - View->ViewLocation);
				ViewDirection.Normalize();

				bool bWorldAxis = (bExternalWorldLocalState) ? (*bExternalWorldLocalState) : false;
				FVector ArrowDirection = (bWorldAxis) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };
				bool bFlipped = (FVector::DotProduct(ViewDirection, ArrowDirection) > 0);
				ArrowDirection = (bFlipped) ? -ArrowDirection : ArrowDirection;
				if (bIsFocusedView && bFlippedExternal != nullptr)
				{
					*bFlippedExternal = bFlipped;
				}

				//bRenderVisibility = FMath::Abs(FVector::DotProduct(ArrowDirection, ViewDirection)) < 0.985f;   // ~10 degrees
				bool bRenderVisibility = FMath::Abs(FVector::DotProduct(ArrowDirection, ViewDirection)) < 0.965f;   // ~15 degrees

				if (bIsFocusedView && bExternalRenderVisibility != nullptr)
				{
					*bExternalRenderVisibility = bRenderVisibility;
				}
				if (bRenderVisibility == false)
				{
					continue;
				}

				float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(View, Origin);
				float LengthScale = PixelToWorldScale;
				if (bIsFocusedView && ExternalDynamicPixelToWorldScale != nullptr)
				{
					*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
				}

				float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
					(HoverThicknessMultiplier * Thickness) : (Thickness);
				if (!bIsOrtho)
				{
					UseThickness *= (View->FOV / 90.0);		// compensate for FOV scaling in Gizmos...
				}

				double StartDist = LengthScale * Gap;
				double EndDist = LengthScale * (Gap + Length);

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

	// set on Component for use in ::GetDynamicMeshElements()
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;

	// set in ::GetDynamicMeshElements() for use by Component hit testing
	bool* bFlippedExternal = nullptr;
	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalRenderVisibility = nullptr;
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