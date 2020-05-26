// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/GizmoBoxComponent.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "PrimitiveSceneProxy.h"


class FGizmoBoxComponentSceneProxy final : public FPrimitiveSceneProxy
{
public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	FGizmoBoxComponentSceneProxy(const UGizmoBoxComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent),
		Color(InComponent->Color),
		LocalCenter(InComponent->Origin),
		DirectionX(InComponent->Rotation*FVector(1,0,0)),
		DirectionY(InComponent->Rotation*FVector(0,1,0)),
		DirectionZ(InComponent->Rotation*FVector(0,0,1)),
		Dimensions(InComponent->Dimensions),
		Thickness(InComponent->LineThickness),
		HoverThicknessMultiplier(InComponent->HoverSizeMultiplier),
		bEnableFlipping(InComponent->bEnableAxisFlip),
		bRemoveHiddenLines(InComponent->bRemoveHiddenLines)
	{
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
	{
		// try to find focused scene view. May return nullptr.
		const FSceneView* FocusedView = GizmoRenderingUtil::FindFocusedEditorSceneView(Views, ViewFamily, VisibilityMap);

		const FMatrix& LocalToWorldMatrix = GetLocalToWorld();
		FVector WorldOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

		FVector Points[8];   // box corners, order is 000, 100, 110, 010,  001, 101, 111, 011
		static const int Lines[12][2] = {
			{0,1}, {1,2}, {2,3}, {3,0},
			{4,5}, {5,6}, {6,7}, {7,4},
			{0,4}, {1,5}, {2,6}, {3,7}
		};

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				bool bIsFocusedView = (FocusedView != nullptr && View == FocusedView);
				bool bIsOrtho = !View->IsPerspectiveProjection();

				float PixelToWorldScale = GizmoRenderingUtil::CalculateLocalPixelToWorldScale(View, WorldOrigin);
				float LengthScale = PixelToWorldScale;
				if (bIsFocusedView && ExternalDynamicPixelToWorldScale != nullptr)
				{
					*ExternalDynamicPixelToWorldScale = PixelToWorldScale;
				}

				// direction to origin of gizmo
				FVector ViewDirection = 
					(bIsOrtho) ? (View->GetViewDirection()) : (WorldOrigin - View->ViewLocation);
				ViewDirection.Normalize();

				bool bWorldAxis = (bExternalWorldLocalState) ? (*bExternalWorldLocalState) : false;

				FVector UseDirectionX = (bWorldAxis) ? DirectionX : FVector{ LocalToWorldMatrix.TransformVector(DirectionX) };
				bool bFlippedX = (FVector::DotProduct(ViewDirection, UseDirectionX) > 0);
				UseDirectionX = (bEnableFlipping && bFlippedX) ? -UseDirectionX : UseDirectionX;
				if (bIsFocusedView && bFlippedXExternal != nullptr)
				{
					*bFlippedXExternal = bFlippedX;
				}

				FVector UseDirectionY = (bWorldAxis) ? DirectionY : FVector{ LocalToWorldMatrix.TransformVector(DirectionY) };
				bool bFlippedY = (FVector::DotProduct(ViewDirection, UseDirectionY) > 0);
				UseDirectionY = (bEnableFlipping && bFlippedY) ? -UseDirectionY : UseDirectionY;
				if (bIsFocusedView && bFlippedYExternal != nullptr)
				{
					*bFlippedYExternal = bFlippedY;
				}

				FVector UseDirectionZ = (bWorldAxis) ? DirectionZ : FVector{ LocalToWorldMatrix.TransformVector(DirectionZ) };
				bool bFlippedZ = (FVector::DotProduct(ViewDirection, UseDirectionZ) > 0);
				UseDirectionZ = (bEnableFlipping && bFlippedZ) ? -UseDirectionZ : UseDirectionZ;
				if (bIsFocusedView && bFlippedZExternal != nullptr)
				{
					*bFlippedZExternal = bFlippedZ;
				}

				FVector UseCenter(
					(bFlippedX) ? -LocalCenter.X : LocalCenter.X,
					(bFlippedY) ? -LocalCenter.Y : LocalCenter.Y,
					(bFlippedZ) ? -LocalCenter.Z : LocalCenter.Z
				);
				FVector WorldCenter = WorldOrigin
					+ LengthScale * LocalCenter.X * UseDirectionX
					+ LengthScale * LocalCenter.Y * UseDirectionY
					+ LengthScale * LocalCenter.Z * UseDirectionZ;
				//= LocalToWorldMatrix.TransformPosition(LengthScale*UseCenter);

				float UseThickness = (bExternalHoverState != nullptr && *bExternalHoverState == true) ?
					(HoverThicknessMultiplier * Thickness) : (Thickness);
				if (!bIsOrtho)
				{
					UseThickness *= (View->FOV / 90.0);		// compensate for FOV scaling in Gizmos...
				}

				double DimensionX = LengthScale * Dimensions.X * 0.5f;
				double DimensionY = LengthScale * Dimensions.Y * 0.5f;
				double DimensionZ = LengthScale * Dimensions.Z * 0.5f;

				Points[0] = - DimensionX*UseDirectionX - DimensionY*UseDirectionY - DimensionZ*UseDirectionZ;
				Points[1] = + DimensionX*UseDirectionX - DimensionY*UseDirectionY - DimensionZ*UseDirectionZ;
				Points[2] = + DimensionX*UseDirectionX + DimensionY*UseDirectionY - DimensionZ*UseDirectionZ;
				Points[3] = - DimensionX*UseDirectionX + DimensionY*UseDirectionY - DimensionZ*UseDirectionZ;

				Points[4] = - DimensionX*UseDirectionX - DimensionY*UseDirectionY + DimensionZ*UseDirectionZ;
				Points[5] = + DimensionX*UseDirectionX - DimensionY*UseDirectionY + DimensionZ*UseDirectionZ;
				Points[6] = + DimensionX*UseDirectionX + DimensionY*UseDirectionY + DimensionZ*UseDirectionZ;
				Points[7] = - DimensionX*UseDirectionX + DimensionY*UseDirectionY + DimensionZ*UseDirectionZ;

				if (bRemoveHiddenLines)
				{
					// find box corner direction that is most aligned with view direction. That's the corner we will hide.
					float MaxDot = -999999;
					int MaxDotIndex = -1;
					for (int j = 0; j < 8; ++j)
					{
						float Dot = FVector::DotProduct(Points[j], ViewDirection);
						if (Dot > MaxDot)
						{
							MaxDot = Dot;
							MaxDotIndex = j;
						}
						Points[j] += WorldCenter;
					}
					for (int j = 0; j < 12; ++j)
					{
						if (Lines[j][0] != MaxDotIndex && Lines[j][1] != MaxDotIndex)
						{
							PDI->DrawLine(Points[Lines[j][0]], Points[Lines[j][1]], Color, SDPG_Foreground, UseThickness, 0.0f, true);
						}
					}
				}
				else
				{
					for (int j = 0; j < 8; ++j)
					{
						Points[j] += WorldCenter;
					}
					for (int j = 0; j < 12; ++j)
					{
						PDI->DrawLine(Points[Lines[j][0]], Points[Lines[j][1]], Color, SDPG_Foreground, UseThickness, 0.0f, true);
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

	void SetExternalFlip(bool* bFlippedX, bool* bFlippedY, bool* bFlippedZ)
	{
		bFlippedXExternal = bFlippedX;
		bFlippedYExternal = bFlippedY;
		bFlippedZExternal = bFlippedZ;
	}

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
	FVector LocalCenter;
	FVector DirectionX, DirectionY, DirectionZ;;
	FVector Dimensions;
	float Thickness;
	float HoverThicknessMultiplier;
	bool bEnableFlipping = false;
	bool bRemoveHiddenLines = true;

	// set on Component for use in ::GetDynamicMeshElements()
	bool* bExternalHoverState = nullptr;
	bool* bExternalWorldLocalState = nullptr;

	// set in ::GetDynamicMeshElements() for use by Component hit testing
	bool* bFlippedXExternal = nullptr;
	bool* bFlippedYExternal = nullptr;
	bool* bFlippedZExternal = nullptr;
	float* ExternalDynamicPixelToWorldScale = nullptr;
};



FPrimitiveSceneProxy* UGizmoBoxComponent::CreateSceneProxy()
{
	FGizmoBoxComponentSceneProxy* NewProxy = new FGizmoBoxComponentSceneProxy(this);
	if (bEnableAxisFlip)
	{
		NewProxy->SetExternalFlip(&bFlippedX, &bFlippedY, &bFlippedZ);
	}
	NewProxy->SetExternalDynamicPixelToWorldScale(&DynamicPixelToWorldScale);
	NewProxy->SetExternalHoverState(&bHovering);
	NewProxy->SetExternalWorldLocalState(&bWorld);
	return NewProxy;
}

FBoxSphereBounds UGizmoBoxComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(FSphere(Origin, Dimensions.Size()).TransformBy(LocalToWorld));
}

bool UGizmoBoxComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	if (bRenderVisibility == false)
	{
		return false;
	}

	const FTransform& Transform = this->GetComponentToWorld();

	// transform points into local space
	FVector StartLocal = Transform.InverseTransformPosition(Start);
	FVector EndLocal = Transform.InverseTransformPosition(End);

	// transform into rotation space
	FQuat InvRotation = Rotation.Inverse();
	StartLocal = InvRotation * StartLocal;
	EndLocal = InvRotation * EndLocal;

	FVector UseOrigin(
		(bEnableAxisFlip && bFlippedX) ? -Origin.X : Origin.X,
		(bEnableAxisFlip && bFlippedY) ? -Origin.Y : Origin.Y,
		(bEnableAxisFlip && bFlippedZ) ? -Origin.Z : Origin.Z
	);
	UseOrigin *= DynamicPixelToWorldScale;

	FVector ScaledDims = DynamicPixelToWorldScale * Dimensions;
	FBox Box(UseOrigin-0.5f*ScaledDims, UseOrigin+0.5f*ScaledDims);

	//FMath::LineBoxIntersection(Box, StartLocal, EndLocal, 
	FVector Extent(SMALL_NUMBER, SMALL_NUMBER, SMALL_NUMBER);
	FVector HitLocal, NormalLocal; float HitTime;
	if (FMath::LineExtentBoxIntersection(Box, StartLocal, EndLocal, Extent, HitLocal, NormalLocal, HitTime) == false)
	{
		return false;
	}

	FVector HitWorld = Transform.TransformPosition(Rotation * HitLocal);

	OutHit.Component = this;
	OutHit.ImpactPoint = HitWorld;
	OutHit.Distance = FVector::Distance(Start, HitWorld);
	//OutHit.ImpactNormal = ;
	
	return true;
}

void UGizmoBoxComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	//OutMaterials.Add(GEngine->VertexColorMaterial);
}