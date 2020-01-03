// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoRectangleComponent.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "PrimitiveSceneProxy.h"


class FGizmoRectangleComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGizmoRectangleComponentSceneProxy(const UGizmoRectangleComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent),
		Color(InComponent->Color),
		DirectionX(InComponent->DirectionX),
		DirectionY(InComponent->DirectionY),
		OffsetX(InComponent->OffsetX),
		OffsetY(InComponent->OffsetY),
		LengthX(InComponent->LengthX),
		LengthY(InComponent->LengthY),
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
		FVector UseDirectionX = (bWorldAxis) ? DirectionX : FVector{ LocalToWorldMatrix.TransformVector(DirectionX) };
		bool bFlippedX = (FVector::DotProduct(ViewDirection, UseDirectionX) > 0);
		UseDirectionX = (bFlippedX) ? -UseDirectionX : UseDirectionX;
		if (bFlippedXExternal)
		{
			*bFlippedXExternal = bFlippedX;
		}

		FVector UseDirectionY = (bWorldAxis) ? DirectionY : FVector{ LocalToWorldMatrix.TransformVector(DirectionY) };
		bool bFlippedY = (FVector::DotProduct(ViewDirection, UseDirectionY) > 0);
		UseDirectionY = (bFlippedY) ? -UseDirectionY : UseDirectionY;
		if (bFlippedYExternal)
		{
			*bFlippedYExternal = bFlippedY;
		}

		if (bExternalRenderVisibility != nullptr)
		{
			FVector PlaneNormal = FVector::CrossProduct(UseDirectionX, UseDirectionY);
			*bExternalRenderVisibility = FMath::Abs(FVector::DotProduct(PlaneNormal, ViewDirection)) > 0.25f;
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

		double UseOffsetX = LengthScale * OffsetX;
		double UseOffsetLengthX = LengthScale * (OffsetX + LengthX);
		double UseOffsetY = LengthScale * OffsetY;
		double UseOffsetLengthY = LengthScale * (OffsetY + LengthY);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);

				FVector Point00 = Origin + UseOffsetX*UseDirectionX + UseOffsetY*UseDirectionY;
				FVector Point10 = Origin + UseOffsetLengthX*UseDirectionX + UseOffsetY*UseDirectionY;
				FVector Point11 = Origin + UseOffsetLengthX*UseDirectionX + UseOffsetLengthY*UseDirectionY;
				FVector Point01 = Origin + UseOffsetX*UseDirectionX + UseOffsetLengthY*UseDirectionY;

				PDI->DrawLine(Point10, Point11, Color, SDPG_Foreground, UseThickness, 0.0f, true);
				PDI->DrawLine(Point11, Point01, Color, SDPG_Foreground, UseThickness, 0.0f, true);


				/* 
				 
				// Draw solid square. Will be occluded but can be used w/ custom depth to show hidden
				 
				FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());
				MeshBuilder.AddVertex(Point00, FVector2D::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, UseColor);
				MeshBuilder.AddVertex(Point10, FVector2D::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, UseColor);
				MeshBuilder.AddVertex(Point11, FVector2D::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, UseColor);
				MeshBuilder.AddVertex(Point01, FVector2D::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, UseColor);
				MeshBuilder.AddTriangle(0, 1, 2);
				MeshBuilder.AddTriangle(0, 2, 3);

				//FMaterialRenderProxy* MaterialRenderProxy = Material->GetRenderProxy();
				FMaterialRenderProxy* MaterialRenderProxy = GEngine->VertexColorMaterial->GetRenderProxy();
				MeshBuilder.GetMesh(FMatrix::Identity, MaterialRenderProxy, SDPG_Foreground, true, false, ViewIndex, Collector);
				//MeshBuilder.GetMesh(FMatrix::Identity, MaterialRenderProxy, SDPG_World, true, false, ViewIndex, Collector);
				*/
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

	void SetExternalFlip(bool* bFlippedX, bool* bFlippedY)
	{
		bFlippedXExternal = bFlippedX;
		bFlippedYExternal = bFlippedY;
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
	FVector DirectionX, DirectionY;
	float OffsetX, OffsetY;
	float LengthX, LengthY;
	float Thickness;
	float HoverThicknessMultiplier;

	bool* bFlippedXExternal = nullptr;
	bool* bFlippedYExternal = nullptr;
	float* ExternalDynamicPixelToWorldScale = nullptr;
	bool* bExternalRenderVisibility = nullptr;
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;
};



FPrimitiveSceneProxy* UGizmoRectangleComponent::CreateSceneProxy()
{
	FGizmoRectangleComponentSceneProxy* NewProxy = new FGizmoRectangleComponentSceneProxy(this);
	NewProxy->SetExternalFlip(&bFlippedX, &bFlippedY);
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
	NewProxy->SetExternalRenderVisibility(&bRenderVisibility);
	NewProxy->SetExternalHoverState(&bHovering);
	NewProxy->SetExternalWorldLocalState(&bWorld);
	return NewProxy;
}

FBoxSphereBounds UGizmoRectangleComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	float MaxOffset = FMath::Max(OffsetX, OffsetY);
	float MaxLength = FMath::Max(LengthX, LengthY);
	return FBoxSphereBounds(FSphere(FVector::ZeroVector, 100*MaxOffset+MaxLength).TransformBy(LocalToWorld));
}

bool UGizmoRectangleComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	if (bRenderVisibility == false)
	{
		return false;
	}

	const FTransform& Transform = this->GetComponentToWorld();

	FVector UseDirectionX = (bFlippedX) ? -DirectionX : DirectionX;
	UseDirectionX = (bWorld) ? UseDirectionX : Transform.TransformVector(UseDirectionX);
	FVector UseDirectionY = (bFlippedY) ? -DirectionY : DirectionY;
	UseDirectionY = (bWorld) ? DirectionY : Transform.TransformVector(UseDirectionY);
	FVector UseOrigin = Transform.TransformPosition(FVector::ZeroVector);

	float LengthScale = DynamicPixelToWorldScale;
	double UseOffsetX = LengthScale * OffsetX;
	double UseOffsetLengthX = LengthScale * (OffsetX + LengthX);
	double UseOffsetY = LengthScale * OffsetY;
	double UseOffsetLengthY = LengthScale * (OffsetY + LengthY);

	FVector Points[4] = {
		UseOrigin + UseOffsetX*UseDirectionX + UseOffsetY*UseDirectionY,
		UseOrigin + UseOffsetLengthX*UseDirectionX + UseOffsetY*UseDirectionY,
		UseOrigin + UseOffsetLengthX*UseDirectionX + UseOffsetLengthY*UseDirectionY,
		UseOrigin + UseOffsetX*UseDirectionX + UseOffsetLengthY*UseDirectionY
	};
	static const int Triangles[2][3] = { {0,1,2}, {0,2,3} };

	for (int j = 0; j < 2; ++j)
	{
		const int* Triangle = Triangles[j];
		FVector HitPoint, HitNormal;
		if (FMath::SegmentTriangleIntersection(Start, End,
			Points[Triangle[0]], Points[Triangle[1]], Points[Triangle[2]], 
			HitPoint, HitNormal) )
		{
			OutHit.Component = this;
			OutHit.Distance = FVector::Distance(Start, HitPoint);
			OutHit.ImpactPoint = HitPoint;
			OutHit.ImpactNormal = HitNormal;
			return true;
		}
	}

	return false;
}

void UGizmoRectangleComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	//OutMaterials.Add(GEngine->VertexColorMaterial);
}