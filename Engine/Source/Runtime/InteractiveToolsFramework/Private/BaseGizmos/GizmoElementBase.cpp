// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementBase.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogGizmoElementBase, Log, All);

bool UGizmoElementBase::GetViewDependentVisibility(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	if (ViewDependentType == EGizmoElementViewDependentType::None || ViewAlignType == EGizmoElementViewAlignType::PointOnly || ViewAlignType == EGizmoElementViewAlignType::PointEye)
	{
		return true;
	}

	FVector ViewDir;

	if (View->IsPerspectiveProjection())
	{

		FVector WorldCenter = InLocalToWorldTransform.TransformPosition(InLocalCenter);
		ViewDir = WorldCenter - View->ViewLocation;
		ViewDir.Normalize();
	}
	else
	{
		ViewDir = View->GetViewDirection();
	}

	bool bVisibleViewDependent;
	if (ViewDependentType == EGizmoElementViewDependentType::Axis)
	{
		bVisibleViewDependent = FMath::Abs(FVector::DotProduct(ViewDependentAxis, ViewDir)) < DefaultViewAlignAxialMaxCosAngleTol;
	}
	else // (ViewDependentType == EGizmoElementViewDependentType::Plane)
	{
		bVisibleViewDependent = FMath::Abs(FVector::DotProduct(ViewDependentAxis, ViewDir)) > DefaultViewAlignPlanarMinCosAngleTol;
	}

	return bVisibleViewDependent;

}


bool UGizmoElementBase::GetViewAlignRot(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter, FQuat& OutAlignRot) const
{
	if (ViewAlignType == EGizmoElementViewAlignType::None)
	{
		return false;
	}

	FVector Scale = InLocalToWorldTransform.GetScale3D();
	if (!FMath::IsNearlyEqual(Scale.X, Scale.Y, KINDA_SMALL_NUMBER) || !FMath::IsNearlyEqual(Scale.X, Scale.Z, KINDA_SMALL_NUMBER))
	{
		// Warn that non-uniform scale is not currently supported 
		bool bNonUniformScaleWarning = true;
		if (bNonUniformScaleWarning)
		{
			UE_LOG(LogGizmoElementBase, Warning, TEXT("Gizmo element library view-dependent alignment does not currently support non-uniform scale (%f %f %f)."),
				Scale.X, Scale.Y, Scale.Z);
			bNonUniformScaleWarning = false;
		}
		return false;
	}

	FVector LocalViewDir;

	FTransform WorldToLocalTransform = InLocalToWorldTransform.Inverse();

	if (View->IsPerspectiveProjection() && ViewAlignType != EGizmoElementViewAlignType::PointScreen)
	{
		FVector LocalViewLocation = WorldToLocalTransform.TransformPosition(View->ViewLocation);
		LocalViewDir = InLocalCenter - LocalViewLocation;
		LocalViewDir.Normalize();
	}
	else
	{
		FVector WorldViewDir = View->GetViewDirection();
		LocalViewDir = WorldToLocalTransform.GetRotation().RotateVector(WorldViewDir);
		LocalViewDir.Normalize();
	}
		
	if (ViewAlignType == EGizmoElementViewAlignType::PointOnly)
	{
		OutAlignRot = FQuat::FindBetweenNormals(ViewAlignNormal, -LocalViewDir);
	}
	else if (ViewAlignType == EGizmoElementViewAlignType::PointEye || ViewAlignType == EGizmoElementViewAlignType::PointScreen)
	{
		FVector Right = ViewAlignAxis ^ ViewAlignNormal;
		Right.Normalize();
		FVector Up = ViewAlignNormal ^ Right;

		FVector LocalViewUp = WorldToLocalTransform.TransformVector(View->GetViewUp());
		FVector TargetFwd = -LocalViewDir;
		FVector TargetRight = LocalViewUp ^ TargetFwd;
		TargetRight.Normalize();
		FVector TargetUp = TargetFwd ^ TargetRight;

		OutAlignRot = GetAlignRotBetweenCoordSpaces(ViewAlignNormal, Right, Up, TargetFwd, TargetRight, TargetUp);
	}
	else if (ViewAlignType == EGizmoElementViewAlignType::Axial)
	{
		// if Axis and Dir are almost coincident, do not adjust the rotation
		if ((FMath::Abs(FVector::DotProduct(ViewAlignAxis, -LocalViewDir))) >= DefaultViewAlignAxialMaxCosAngleTol)
		{
			return false;
		}

		FVector TargetRight = -LocalViewDir ^ ViewAlignAxis;
		TargetRight.Normalize();
		FVector TargetNormal = ViewAlignAxis ^ TargetRight;
		TargetNormal.Normalize();
		OutAlignRot = FQuat::FindBetweenNormals(ViewAlignNormal, TargetNormal);
	}

	return true;
}

FQuat UGizmoElementBase::GetAlignRotBetweenCoordSpaces(FVector SourceForward, FVector SourceRight, FVector SourceUp, FVector TargetForward, FVector TargetRight, FVector TargetUp) const
{
	FMatrix SourceToCanonical(
		FPlane(SourceForward.X, SourceRight.X, SourceUp.X, 0.0),
		FPlane(SourceForward.Y, SourceRight.Y, SourceUp.Y, 0.0),
		FPlane(SourceForward.Z, SourceRight.Z, SourceUp.Z, 0.0),
		FPlane::ZeroVector);

	FMatrix CanonicalToTarget(
		FPlane(TargetForward.X, TargetForward.Y, TargetForward.Z, 0.0),
		FPlane(TargetRight.X, TargetRight.Y, TargetRight.Z, 0.0),
		FPlane(TargetUp.X, TargetUp.Y, TargetUp.Z, 0.0),
		FPlane::ZeroVector);

	FMatrix SourceToTarget = SourceToCanonical * CanonicalToTarget;
	FQuat Result = SourceToTarget.ToQuat();
	Result.Normalize();
	return Result;
}

const UMaterialInterface* UGizmoElementBase::GetCurrentMaterial(const FRenderTraversalState& RenderState) const
{
	EGizmoElementInteractionState CurrentState;

	if (RenderState.InteractionState == EGizmoElementInteractionState::None)
	{
		CurrentState = ElementInteractionState;
	}
	else
	{
		CurrentState = RenderState.InteractionState;
	}

	if (CurrentState == EGizmoElementInteractionState::Hovering)
	{
		if (RenderState.HoverMaterial.IsValid())
		{
			return RenderState.HoverMaterial.Get();
		}
		return HoverMaterial;
	}

	if (CurrentState == EGizmoElementInteractionState::Interacting)
	{
		if (RenderState.InteractMaterial.IsValid())
		{
			return RenderState.InteractMaterial.Get();
		}
		return InteractMaterial;
	}

	// CurrentState is None, so just return the regular material
	if (RenderState.Material.IsValid())
	{
		return RenderState.Material.Get();
	}
	return Material;
}

void UGizmoElementBase::CacheRenderState(const FTransform& InLocalToWorldState, double InPixelToWorldScale, bool InVisibleViewDependent)
{
	CachedLocalToWorldTransform = InLocalToWorldState;
	CachedPixelToWorldScale = InPixelToWorldScale;
	bHasCachedLocalToWorldTransform = true;
	bCachedVisibleViewDependent = InVisibleViewDependent;
}

void UGizmoElementBase::ResetCachedRenderState()
{
	bHasCachedLocalToWorldTransform = false;
	CachedLocalToWorldTransform = FTransform::Identity;
	CachedPixelToWorldScale = 1.0f;
	bCachedVisibleViewDependent = true;
}

void UGizmoElementBase::UpdateRenderTraversalState(FRenderTraversalState& InRenderTraversalState)
{
	if (InRenderTraversalState.InteractionState == EGizmoElementInteractionState::None)
	{
		InRenderTraversalState.InteractionState = ElementInteractionState;
	}
	if (!InRenderTraversalState.HoverMaterial.IsValid())
	{
		InRenderTraversalState.HoverMaterial = HoverMaterial;
	}
	if (!InRenderTraversalState.InteractMaterial.IsValid())
	{
		InRenderTraversalState.InteractMaterial = InteractMaterial;
	}
	if (!InRenderTraversalState.Material.IsValid())
	{
		InRenderTraversalState.Material = Material;
	}
}

bool UGizmoElementBase::IsVisible() const
{
	bool bVisible = static_cast<uint8>(ElementState) & static_cast<uint8>(EGizmoElementState::Visible);
	return (bEnabled && bVisible);
}

bool UGizmoElementBase::IsHittable() const
{
	bool bHittable = static_cast<uint8>(ElementState) & static_cast<uint8>(EGizmoElementState::Hittable);
	return (bEnabled && bHittable);
}

bool UGizmoElementBase::IsHittableInView() const
{
	return (IsHittable() && bHasCachedLocalToWorldTransform && (!IsVisible() || bCachedVisibleViewDependent));
}

void UGizmoElementBase::SetEnabled(bool InEnabled)
{
	bEnabled = InEnabled;
}

bool UGizmoElementBase::GetEnabled() const
{
	return bEnabled;
}
void UGizmoElementBase::SetPartIdentifier(uint32 InPartIdentifier)
{
	PartIdentifier = InPartIdentifier;
}

uint32 UGizmoElementBase::GetPartIdentifier()
{
	return PartIdentifier;
}

void UGizmoElementBase::SetElementState(EGizmoElementState InElementState)
{
	ElementState = InElementState;
}

EGizmoElementState UGizmoElementBase::GetElementState() const
{
	return ElementState;
}

void UGizmoElementBase::SetElementInteractionState(EGizmoElementInteractionState InElementInteractionState)
{
	ElementInteractionState = InElementInteractionState;
}

EGizmoElementInteractionState UGizmoElementBase::GetElementInteractionState() const
{
	return ElementInteractionState;
}

void UGizmoElementBase::UpdatePartHittableState(bool bHittable, uint32 InPartIdentifier)
{
	if (InPartIdentifier == PartIdentifier)
	{
		uint8 State = static_cast<uint8>(ElementState);
		uint8 HittableMask = static_cast<uint8>(EGizmoElementState::Hittable);
		uint8 NewState = (bHittable ? State | HittableMask : State & (~HittableMask));
		ElementState = static_cast<EGizmoElementState>(NewState);
	}
}

void UGizmoElementBase::UpdatePartVisibleState(bool bVisible, uint32 InPartIdentifier)
{
	if (InPartIdentifier == PartIdentifier)
	{
		uint8 State = static_cast<uint8>(ElementState);
		uint8 VisibleMask = static_cast<uint8>(EGizmoElementState::Visible);
		uint8 NewState = (bVisible ? State | VisibleMask : State & (~VisibleMask));
		ElementState = static_cast<EGizmoElementState>(NewState);
	}
}

void UGizmoElementBase::UpdatePartInteractionState(EGizmoElementInteractionState InInteractionState, uint32 InPartIdentifier)
{
	if (InPartIdentifier == PartIdentifier)
	{
		ElementInteractionState = InInteractionState;
	}
}

void UGizmoElementBase::SetViewDependentType(EGizmoElementViewDependentType InViewDependentType)
{
	ViewDependentType = InViewDependentType;
}

EGizmoElementViewDependentType UGizmoElementBase::GetViewDependentType() const
{
	return ViewDependentType;
}

void UGizmoElementBase::SetViewDependentAngleTol(float InAngleTol)
{
	ViewDependentAngleTol = InAngleTol;
	ViewDependentAxialMaxCosAngleTol = FMath::Abs(FMath::Cos(ViewDependentAngleTol));
	ViewDependentPlanarMinCosAngleTol = FMath::Abs(FMath::Cos(HALF_PI + ViewDependentAngleTol));
}

float UGizmoElementBase::GetViewDependentAngleTol() const
{
	return ViewDependentAngleTol;
}

void UGizmoElementBase::SetViewDependentAxis(FVector InViewDependentAxis)
{
	ViewDependentAxis = InViewDependentAxis;
	ViewDependentAxis.Normalize();
}

FVector UGizmoElementBase::GetViewDependentAxis() const
{
	return ViewDependentAxis;
}

void UGizmoElementBase::SetViewAlignType(EGizmoElementViewAlignType InViewAlignType)
{
	ViewAlignType = InViewAlignType;
}

EGizmoElementViewAlignType UGizmoElementBase::GetViewAlignType() const
{
	return ViewAlignType;
}

void UGizmoElementBase::SetViewAlignAxis(FVector InViewAlignAxis)
{
	ViewAlignAxis = InViewAlignAxis;
	ViewAlignAxis.Normalize();
}

FVector UGizmoElementBase::GetViewAlignAxis() const
{
	return ViewAlignAxis;
}

void UGizmoElementBase::SetViewAlignNormal(FVector InViewAlignNormal)
{
	ViewAlignNormal = InViewAlignNormal;
	ViewAlignNormal.Normalize();
}

FVector UGizmoElementBase::GetViewAlignNormal() const
{
	return ViewAlignNormal;
}

void UGizmoElementBase::SetViewAlignAxialAngleTol(float InAngleTol)
{
	ViewAlignAxialAngleTol = InAngleTol;
	ViewAlignAxialMaxCosAngleTol = FMath::Abs(FMath::Cos(ViewAlignAxialAngleTol));
}

float UGizmoElementBase::GetViewAlignAxialAngleTol() const
{
	return ViewAlignAxialAngleTol;
}

void UGizmoElementBase::SetMaterial(UMaterialInterface* InMaterial)
{
	Material = InMaterial;
}

UMaterialInterface* UGizmoElementBase::GetMaterial() const
{
	return Material;
}

void UGizmoElementBase::SetHoverMaterial(UMaterialInterface* InHoverMaterial)
{
	HoverMaterial = InHoverMaterial;
}

UMaterialInterface* UGizmoElementBase::GetHoverMaterial() const
{
	return HoverMaterial;
}

void UGizmoElementBase::SetInteractMaterial(UMaterialInterface* InInteractMaterial)
{
	InteractMaterial = InInteractMaterial;
}

UMaterialInterface* UGizmoElementBase::GetInteractMaterial() const
{
	return InteractMaterial;
}

void UGizmoElementBase::SetVertexColor(const FColor& InVertexColor)
{
	VertexColor = InVertexColor;
}

FColor UGizmoElementBase::GetVertexColor() const
{
	return VertexColor;
}

void UGizmoElementBase::SetPixelHitDistanceThreshold(float InPixelHitDistanceThreshold)
{
	PixelHitDistanceThreshold = InPixelHitDistanceThreshold;
}

float UGizmoElementBase::GetPixelHitDistanceThreshold() const
{
	return PixelHitDistanceThreshold;
}

