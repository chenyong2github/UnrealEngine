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
		// find view to use for gizmo sizing/etc
		const FSceneView* GizmoControlView = GizmoRenderingUtil::FindActiveSceneView(Views, ViewFamily, VisibilityMap);
		if (GizmoControlView == nullptr)
		{
			return;
		}

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		FVector PlaneX, PlaneY;
		GizmoMath::MakeNormalPlaneBasis(Normal, PlaneX, PlaneY);

		// direction to origin of gizmo
		FVector GizmoViewDirection = Origin - GizmoControlView->ViewLocation;
		GizmoViewDirection.Normalize();

		float LengthScale = 1.0f;
		if (ExternalDynamicPixelToWorldScale != nullptr)
		{
			float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(GizmoControlView, Origin);
			*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
			LengthScale = PixelToWorldScale;
		}

		double UseRadius = LengthScale * Radius;

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

				const float	AngleDelta = 2.0f * PI / NumSides;

				if (bViewAligned)
				{
					FVector ViewVector = View->GetViewDirection();
					FVector WorldOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
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


private:
	FLinearColor Color;
	FVector Normal;
	float Radius;
	float Thickness;
	int NumSides;
	bool bViewAligned;
	float HoverThicknessMultiplier;

	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;
};




FPrimitiveSceneProxy* UGizmoCircleComponent::CreateSceneProxy()
{
	FGizmoCircleComponentSceneProxy* NewProxy = new FGizmoCircleComponentSceneProxy(this);
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
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
	float LengthScale = DynamicPixelToWorldScale;
	double UseRadius = LengthScale * Radius;

	const FTransform& Transform = this->GetComponentToWorld();
	FVector WorldNormal = (bWorld) ? Normal : Transform.TransformVector(Normal);
	FVector WorldOrigin = Transform.TransformPosition(FVector::ZeroVector);
	FPlane CirclePlane(WorldOrigin, WorldNormal);

	FRay Ray(Start, End - Start, false);
	FVector HitPos = FMath::RayPlaneIntersection(Ray.Origin, Ray.Direction, CirclePlane);
	float RayParameter = Ray.GetParameter(HitPos);
	if (RayParameter < 0 || RayParameter > Ray.GetParameter(End))
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
	if (bOnlyAllowFrontFacingHits)
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