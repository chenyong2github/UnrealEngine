// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoCircleComponent.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "PrimitiveSceneProxy.h"


class FGizmoCircleComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGizmoCircleComponentSceneProxy(const UGizmoCircleComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent),
		Color(InComponent->Color),
		Normal(InComponent->Normal),
		Radius(InComponent->Radius),
		Thickness(InComponent->Thickness),
		NumSides(InComponent->NumSides),
		bViewAligned(InComponent->bViewAligned),
		HoverThicknessMultiplier(InComponent->HoverSizeMultiplier)
	{
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		// try to find focused scene view. May return nullptr.
		const FSceneView* FocusedView = GizmoRenderingUtil::FindFocusedEditorSceneView(Views, ViewFamily, VisibilityMap);

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
		FVector PlaneX, PlaneY;
		GizmoMath::MakeNormalPlaneBasis(Normal, PlaneX, PlaneY);

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

				// direction to origin of gizmo
				FVector GizmoViewDirection = 
					(bIsOrtho) ? (View->GetViewDirection()) : (Origin - View->ViewLocation);
				GizmoViewDirection.Normalize();

				float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(View, Origin);
				float LengthScale = PixelToWorldScale;
				if (bIsFocusedView && ExternalDynamicPixelToWorldScale != nullptr)
				{
					*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
				}

				double UseRadius = LengthScale * Radius;

				FLinearColor BackColor = FLinearColor(0.5f, 0.5f, 0.5f);
				float BackThickness = 0.5f;
				float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
					(HoverThicknessMultiplier * Thickness) : (Thickness);
				if (!bIsOrtho)
				{
					UseThickness *= (View->FOV / 90.0);		// compensate for FOV scaling in Gizmos...
					BackThickness *= (View->FOV / 90.0);		// compensate for FOV scaling in Gizmos...
				}

				const float	AngleDelta = 2.0f * PI / NumSides;

				if (bViewAligned)
				{
					//if (bIsOrtho)		// skip in ortho views?
					//{
					//	if (bIsFocusedView && bExternalRenderVisibility != nullptr)
					//	{
					//		*bExternalRenderVisibility = false;
					//	}
					//	continue;
					//}

					FVector WorldOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
					WorldOrigin += 0.001 * ViewVector;
					FVector WorldPlaneX, WorldPlaneY;
					GizmoMath::MakeNormalPlaneBasis(ViewVector, WorldPlaneX, WorldPlaneY);

					FVector	LastVertex = WorldOrigin + WorldPlaneX * UseRadius;
					for (int32 SideIndex = 0; SideIndex < NumSides; SideIndex++)
					{
						float DeltaX = FMath::Cos(AngleDelta * (SideIndex + 1));
						float DeltaY = FMath::Sin(AngleDelta * (SideIndex + 1));
						const FVector DeltaVector = WorldPlaneX * DeltaX + WorldPlaneY * DeltaY;
						const FVector Vertex = WorldOrigin + UseRadius * DeltaVector;
						PDI->DrawLine(LastVertex, Vertex, Color, SDPG_Foreground, UseThickness, 0.0f, true);
						LastVertex = Vertex;
					}
				}
				else 
				{
					FVector WorldOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
					bool bWorldAxis = (bExternalWorldLocalState) ? (*bExternalWorldLocalState) : false;
					FVector WorldPlaneX = (bWorldAxis) ? PlaneX : FVector{ LocalToWorldMatrix.TransformVector(PlaneX) };
					FVector WorldPlaneY = (bWorldAxis) ? PlaneY : FVector{ LocalToWorldMatrix.TransformVector(PlaneY) };

					FVector PlaneWorldNormal = (bWorldAxis) ? Normal : FVector{ LocalToWorldMatrix.TransformVector(Normal) };
					double ViewDot = FVector::DotProduct(GizmoViewDirection, PlaneWorldNormal);
					bool bOnEdge = FMath::Abs(ViewDot) < 0.05;
					bool bIsViewPlaneParallel = FMath::Abs(ViewDot) > 0.95;
					if (bIsFocusedView && bExternalIsViewPlaneParallel != nullptr)
					{
						*bExternalIsViewPlaneParallel = bIsViewPlaneParallel;
					}

					bool bRenderVisibility = !bOnEdge;
					if (bIsFocusedView && bExternalRenderVisibility != nullptr)
					{
						*bExternalRenderVisibility = bRenderVisibility;
					}
					if (bRenderVisibility)
					{
						if (bIsViewPlaneParallel)
						{
							FVector	LastVertex = WorldOrigin + WorldPlaneX * UseRadius;
							for (int32 SideIndex = 0; SideIndex < NumSides; SideIndex++)
							{
								float DeltaX = FMath::Cos(AngleDelta * (SideIndex + 1));
								float DeltaY = FMath::Sin(AngleDelta * (SideIndex + 1));
								const FVector DeltaVector = WorldPlaneX * DeltaX + WorldPlaneY * DeltaY;
								const FVector Vertex = WorldOrigin + UseRadius * DeltaVector;
								PDI->DrawLine(LastVertex, Vertex, Color, SDPG_Foreground, UseThickness, 0.0f, true);
								LastVertex = Vertex;
							}
						}
						else
						{
							FVector	LastVertex = WorldOrigin + WorldPlaneX * UseRadius;
							bool bLastVisible = FVector::DotProduct(WorldPlaneX, GizmoViewDirection) < 0;
							for (int32 SideIndex = 0; SideIndex < NumSides; SideIndex++)
							{
								float DeltaX = FMath::Cos(AngleDelta * (SideIndex + 1));
								float DeltaY = FMath::Sin(AngleDelta * (SideIndex + 1));
								const FVector DeltaVector = WorldPlaneX * DeltaX + WorldPlaneY * DeltaY;
								const FVector Vertex = WorldOrigin + UseRadius * DeltaVector;
								bool bVertexVisible = FVector::DotProduct(DeltaVector, GizmoViewDirection) < 0;
								if (bLastVisible && bVertexVisible)
								{
									PDI->DrawLine(LastVertex, Vertex, Color, SDPG_Foreground, UseThickness, 0.0f, true);
								}
								else
								{
									PDI->DrawLine(LastVertex, Vertex, BackColor, SDPG_Foreground, BackThickness, 0.0f, true);
								}
								bLastVisible = bVertexVisible;
								LastVertex = Vertex;
							}
						}
					}
				}
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

	void SetExternalRenderVisibility(bool* bRenderVisibility)
	{
		bExternalRenderVisibility = bRenderVisibility;
	}

	void SetExternalIsViewPlaneParallel(bool* bIsViewPlaneParallel)
	{
		bExternalIsViewPlaneParallel = bIsViewPlaneParallel;
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
	FVector Normal;
	float Radius;
	float Thickness;
	int NumSides;
	bool bViewAligned;
	float HoverThicknessMultiplier;

	// set on Component for use in ::GetDynamicMeshElements()
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;

	// set in ::GetDynamicMeshElements() for use by Component hit testing
	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalRenderVisibility = nullptr;
	bool* bExternalIsViewPlaneParallel = nullptr;
};




FPrimitiveSceneProxy* UGizmoCircleComponent::CreateSceneProxy()
{
	FGizmoCircleComponentSceneProxy* NewProxy = new FGizmoCircleComponentSceneProxy(this);
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
	NewProxy->SetExternalIsViewPlaneParallel(&bCircleIsViewPlaneParallel);
	NewProxy->SetExternalRenderVisibility(&bRenderVisibility);
	NewProxy->SetExternalHoverState(&bHovering);
	NewProxy->SetExternalWorldLocalState(&bWorld);
	return NewProxy;
}

FBoxSphereBounds UGizmoCircleComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FSphere(FVector::ZeroVector, Radius).TransformBy(LocalToWorld));
}

bool UGizmoCircleComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	if (bRenderVisibility == false)
	{
		return false;
	}

	float LengthScale = DynamicPixelToWorldScale;
	double UseRadius = LengthScale * Radius;

	const FTransform& Transform = this->GetComponentToWorld();
	FVector WorldNormal = (bWorld) ? Normal : Transform.TransformVector(Normal);
	FVector WorldOrigin = Transform.TransformPosition(FVector::ZeroVector);

	FRay Ray(Start, End - Start, false);

	// Find the intresection with the circle plane. Note that unlike the FMath version, GizmoMath::RayPlaneIntersectionPoint() 
	// checks that the ray isn't parallel to the plane.
	bool bIntersects;
	FVector HitPos;
	GizmoMath::RayPlaneIntersectionPoint(WorldOrigin, WorldNormal, Ray.Origin, Ray.Direction, bIntersects, HitPos);
	if (!bIntersects || Ray.GetParameter(HitPos) > Ray.GetParameter(End))
	{
		return false;
	}

	FVector NearestCircle;
	GizmoMath::ClosetPointOnCircle(HitPos, WorldOrigin, WorldNormal, UseRadius, NearestCircle);

	FVector NearestRay = Ray.ClosestPoint(NearestCircle);

	double Distance = FVector::Distance(NearestCircle, NearestRay);
	if (Distance > PixelHitDistanceThreshold*DynamicPixelToWorldScale)
	{
		return false;
	}

	// filter out hits on "back" of sphere that circle lies on
	if (bOnlyAllowFrontFacingHits && bCircleIsViewPlaneParallel == false)
	{
		bool bSphereIntersects = false;
		FVector SphereHitPoint;
		FVector RayToCirclePointDirection = (NearestCircle - Ray.Origin);
		RayToCirclePointDirection.Normalize();
		GizmoMath::RaySphereIntersection(
			WorldOrigin, UseRadius, Ray.Origin, RayToCirclePointDirection, bSphereIntersects, SphereHitPoint);
		if (bSphereIntersects)
		{
			if (FVector::DistSquared(SphereHitPoint, NearestCircle) > UseRadius*0.1f)
			{
				return false;
			}
		}
	}

	OutHit.Component = this;
	OutHit.Distance = FVector::Distance(Start, NearestRay);
	OutHit.ImpactPoint = NearestRay;
	return true;
}