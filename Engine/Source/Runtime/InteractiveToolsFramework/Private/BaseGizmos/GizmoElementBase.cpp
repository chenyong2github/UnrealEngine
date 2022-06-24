// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "BaseGizmos/GizmoElementBase.h"
#include "BaseGizmos/GizmoInterfaces.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoViewContext.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogGizmoElementBase, Log, All);

namespace GizmoElementBaseLocals
{
	template <class ViewType>
	void GetViewInfo(const ViewType* View,
		FVector& OutViewLocation, FVector& OutViewDirection, FVector& OutViewUp, bool& OutIsPerspectiveView)
	{
		OutIsPerspectiveView = View->IsPerspectiveProjection();
		OutViewLocation = View->ViewLocation;
		OutViewDirection = View->GetViewDirection();
		OutViewUp = View->GetViewUp();
	}
}

bool UGizmoElementBase::GetViewDependentVisibility(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<FSceneView>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewDependentVisibility(ViewLocation, ViewDirection, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter);
}

bool UGizmoElementBase::GetViewDependentVisibility(const UGizmoViewContext* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<UGizmoViewContext>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewDependentVisibility(ViewLocation, ViewDirection, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter);
}


bool UGizmoElementBase::GetViewDependentHittable(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<FSceneView>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewDependentHittable(ViewLocation, ViewDirection, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter);
}

bool UGizmoElementBase::GetViewDependentHittable(const UGizmoViewContext* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<UGizmoViewContext>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewDependentHittable(ViewLocation, ViewDirection, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter);
}


bool UGizmoElementBase::GetViewAlignRot(const FSceneView* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter, FQuat& OutAlignRot) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<FSceneView>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewAlignRot(ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter, OutAlignRot);
}

bool UGizmoElementBase::GetViewAlignRot(const UGizmoViewContext* View, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter, FQuat& OutAlignRot) const
{
	bool bIsPerspectiveView;
	FVector ViewLocation, ViewDirection, ViewUp;
	GizmoElementBaseLocals::GetViewInfo<UGizmoViewContext>(View, ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView);
	return GetViewAlignRot(ViewLocation, ViewDirection, ViewUp, bIsPerspectiveView, InLocalToWorldTransform, InLocalCenter, OutAlignRot);
}

bool UGizmoElementBase::GetViewDependentVisibility(const FVector& InViewLocation, const FVector& InViewDirection, bool bInPerspectiveView, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	if (ViewDependentType == EGizmoElementViewDependentType::None || ViewAlignType == EGizmoElementViewAlignType::PointOnly || ViewAlignType == EGizmoElementViewAlignType::PointEye)
	{
		return true;
	}

	FVector ViewDir;
	if (bInPerspectiveView)
	{
		FVector WorldCenter = InLocalToWorldTransform.TransformPosition(InLocalCenter);
		ViewDir = WorldCenter - InViewLocation;
	}
	else
	{
		ViewDir = InViewDirection;
	}
	ViewDir.Normalize();

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

bool UGizmoElementBase::GetViewDependentHittable(const FVector& InViewLocation, const FVector& InViewDirection, bool bInPerspectiveView, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter) const
{
	return (IsHittable() && (!IsVisible() || GetViewDependentVisibility(InViewLocation, InViewDirection, bInPerspectiveView, InLocalToWorldTransform, InLocalCenter)));
}

bool UGizmoElementBase::GetViewAlignRot(const FVector& InViewLocation, const FVector& InViewDirection, const FVector& InViewUp, bool bInPerspectiveView, const FTransform& InLocalToWorldTransform, const FVector& InLocalCenter, FQuat& OutAlignRot) const
{
	FVector Scale = InLocalToWorldTransform.GetScale3D();

	if (ViewAlignType == EGizmoElementViewAlignType::None || !VerifyUniformScale(Scale))
	{
		return false;
	}

	FVector LocalViewDir;

	FTransform WorldToLocalTransform = InLocalToWorldTransform.Inverse();

	if (bInPerspectiveView && ViewAlignType != EGizmoElementViewAlignType::PointScreen)
	{
		FVector LocalViewLocation = WorldToLocalTransform.TransformPosition(InViewLocation);
		LocalViewDir = InLocalCenter - LocalViewLocation;
		LocalViewDir.Normalize();
	}
	else
	{
		FVector WorldViewDir = InViewDirection;
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

		FVector LocalViewUp = WorldToLocalTransform.TransformVector(InViewUp);
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

bool UGizmoElementBase::VerifyUniformScale(const FVector& Scale) const
{
	if (!FMath::IsNearlyEqual(Scale.X, Scale.Y, KINDA_SMALL_NUMBER) || !FMath::IsNearlyEqual(Scale.X, Scale.Z, KINDA_SMALL_NUMBER))
	{
		// Log one-time warning that non-uniform scale is not currently supported 
		static bool bNonUniformScaleWarning = true;
		if (bNonUniformScaleWarning)
		{
			UE_LOG(LogGizmoElementBase, Warning, TEXT("Gizmo element library view-dependent alignment does not currently support non-uniform scale (%f %f %f)."),
				Scale.X, Scale.Y, Scale.Z);
			bNonUniformScaleWarning = false;
		}
		return false;
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

bool UGizmoElementBase::UpdateRenderState(IToolsContextRenderAPI* RenderAPI, const FVector& InLocalCenter, FRenderTraversalState& InOutRenderState)
{
	FQuat AlignRot;
	bool bHasAlignRot;
	return UpdateRenderState(RenderAPI, InLocalCenter, InOutRenderState, bHasAlignRot, AlignRot);
}

bool UGizmoElementBase::UpdateRenderState(IToolsContextRenderAPI* RenderAPI, const FVector & InLocalCenter, FRenderTraversalState & InOutRenderState, bool& bOutHasAlignRot, FQuat& OutAlignRot)
{
	check(RenderAPI);
	const FSceneView* View = RenderAPI->GetSceneView();
	check(View);

	OutAlignRot = FQuat::Identity;
	bOutHasAlignRot = false;

	if (InOutRenderState.InteractionState == EGizmoElementInteractionState::None)
	{
		InOutRenderState.InteractionState = ElementInteractionState;
	}

	InOutRenderState.MeshRenderState.Update(MeshRenderAttributes);

	bool bVisibleViewDependent = GetViewDependentVisibility(View, InOutRenderState.LocalToWorldTransform, InLocalCenter);

	if (bVisibleViewDependent)
	{
		bOutHasAlignRot = GetViewAlignRot(View, InOutRenderState.LocalToWorldTransform, InLocalCenter, OutAlignRot);
		InOutRenderState.LocalToWorldTransform = FTransform(OutAlignRot, InLocalCenter) * InOutRenderState.LocalToWorldTransform;
	}

	return bVisibleViewDependent;
}

bool UGizmoElementBase::UpdateLineTraceState(const UGizmoViewContext* ViewContext, const FVector& InLocalCenter, FLineTraceTraversalState& InOutRenderState)
{
	FQuat AlignQuat;
	bool bHasAlignRot;
	return UpdateLineTraceState(ViewContext, InLocalCenter, InOutRenderState, bHasAlignRot, AlignQuat);
}

bool UGizmoElementBase::UpdateLineTraceState(const UGizmoViewContext* ViewContext, const FVector& InLocalCenter, FLineTraceTraversalState& InOutRenderState, bool& bOutHasAlignRot, FQuat& OutAlignRot)
{
	check(ViewContext);

	OutAlignRot = FQuat::Identity;
	bOutHasAlignRot = false;

	bool bHittableViewDependent = GetViewDependentHittable(ViewContext, InOutRenderState.LocalToWorldTransform, InLocalCenter);

	if (bHittableViewDependent)
	{
		bOutHasAlignRot = GetViewAlignRot(ViewContext, InOutRenderState.LocalToWorldTransform, InLocalCenter, OutAlignRot);
		InOutRenderState.LocalToWorldTransform = FTransform(OutAlignRot, InLocalCenter) * InOutRenderState.LocalToWorldTransform;
	}

	return bHittableViewDependent;
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

void UGizmoElementBase::SetPixelHitDistanceThreshold(float InPixelHitDistanceThreshold)
{
	PixelHitDistanceThreshold = InPixelHitDistanceThreshold;
}

float UGizmoElementBase::GetPixelHitDistanceThreshold() const
{
	return PixelHitDistanceThreshold;
}

void UGizmoElementBase::SetMaterial(TWeakObjectPtr<UMaterialInterface> InMaterial, bool InOverridesChildState)
{
	MeshRenderAttributes.Material.SetMaterial(InMaterial, InOverridesChildState);
}

const UMaterialInterface* UGizmoElementBase::GetMaterial() const
{
	return MeshRenderAttributes.Material.GetMaterial();
}

bool UGizmoElementBase::GetMaterialOverridesChildState() const
{
	return MeshRenderAttributes.Material.bOverridesChildState;
}

void UGizmoElementBase::ClearMaterial()
{
	MeshRenderAttributes.Material.Reset();
}

void UGizmoElementBase::SetHoverMaterial(TWeakObjectPtr<UMaterialInterface> InMaterial, bool InOverridesChildState)
{
	MeshRenderAttributes.HoverMaterial.SetMaterial(InMaterial, InOverridesChildState);
}

const UMaterialInterface* UGizmoElementBase::GetHoverMaterial() const
{
	return MeshRenderAttributes.HoverMaterial.GetMaterial();
}

bool UGizmoElementBase::GetHoverMaterialOverridesChildState() const
{
	return MeshRenderAttributes.HoverMaterial.bOverridesChildState;
}

void UGizmoElementBase::ClearHoverMaterial()
{
	MeshRenderAttributes.HoverMaterial.Reset();
}

void UGizmoElementBase::SetInteractMaterial(TWeakObjectPtr<UMaterialInterface> InMaterial, bool InOverridesChildState)
{
	MeshRenderAttributes.InteractMaterial.SetMaterial(InMaterial, InOverridesChildState);
}

const UMaterialInterface* UGizmoElementBase::GetInteractMaterial() const
{
	return MeshRenderAttributes.InteractMaterial.GetMaterial();
}

bool UGizmoElementBase::GetInteractMaterialOverridesChildState() const
{
	return MeshRenderAttributes.InteractMaterial.bOverridesChildState;
}

void UGizmoElementBase::ClearInteractMaterial()
{
	MeshRenderAttributes.InteractMaterial.Reset();
}

void UGizmoElementBase::SetVertexColor(FLinearColor InVertexColor, bool InOverridesChildState)
{
	MeshRenderAttributes.VertexColor.SetColor(InVertexColor, InOverridesChildState);
}

FLinearColor UGizmoElementBase::GetVertexColor() const
{
	return MeshRenderAttributes.VertexColor.GetColor();
}
bool UGizmoElementBase::HasVertexColor() const
{
	return MeshRenderAttributes.VertexColor.bHasValue;
}

bool UGizmoElementBase::GetVertexColorOverridesChildState() const
{
	return MeshRenderAttributes.VertexColor.bOverridesChildState;
}

void UGizmoElementBase::ClearVertexColor()
{
	MeshRenderAttributes.VertexColor.Reset();
}

