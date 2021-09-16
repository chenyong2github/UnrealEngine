// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "EditorGizmos/GizmoCylinderObject.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"

void UGizmoCylinderObject::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (!bVisible)
	{
		return;
	}

	check(RenderAPI);

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const FSceneView* View = RenderAPI->GetSceneView();

	const FMatrix LocalToWorldMatrix = LocalToWorldTransform.ToMatrixNoScale();

	FVector UseOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);

	bool bIsOrtho = !View->IsPerspectiveProjection();

	// direction to origin of gizmo
	FVector ViewDirection =
		(bIsOrtho) ? (View->GetViewDirection()) : (UseOrigin - View->ViewLocation);
	ViewDirection.Normalize();

	FVector UseDirection = (bWorld) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };
	bool bFlipToCompare = (FVector::DotProduct(ViewDirection, UseDirection) > 0);
	FVector CompareDirection = (bFlipToCompare) ? -UseDirection : UseDirection;

	// @todo - make this an object property and static constant on the gizmo
	static const float ViewMaxCosAngle = 0.995f;  // ~5 degrees, cos(0.087 radians)
	bVisibleViewDependent = FMath::Abs(FVector::DotProduct(CompareDirection, ViewDirection)) < ViewMaxCosAngle; 

	if (bVisibleViewDependent)
	{
		// @todo: replace with a call to CalculateLocalPixelToWorldScale instead if possible
		FVector FlattenScale = FVector::OneVector;
		DynamicPixelToWorldScale = GizmoRenderingUtil::CalculateViewDependentScaleAndFlatten(View, UseOrigin, GizmoScale, FlattenScale);

		UMaterialInterface* UseMaterial = Material;
		if (bHovering || bInteracting)
		{
			UseMaterial = CurrentMaterial;
		}
		check(UseMaterial);

		const float HalfLength = Length * 0.5f;
		const FVector CylinderCenter(0, 0, Offset + HalfLength);
		FMatrix AxisRotation = FRotationMatrix::MakeFromZ(UseDirection);
		FMatrix CylinderToWorld = FScaleMatrix(DynamicPixelToWorldScale) * AxisRotation * FTranslationMatrix(UseOrigin) * FScaleMatrix(FlattenScale);

		DrawCylinder(PDI, CylinderToWorld, CylinderCenter, FVector(1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), Radius, HalfLength, NumSides, UseMaterial->GetRenderProxy(), SDPG_Foreground);
	}
}

FInputRayHit UGizmoCylinderObject::LineTraceObject(const FVector RayOrigin, const FVector RayDirection)
{
	if (bVisible && bVisibleViewDependent)
	{
		bool bIntersects = false;
		double RayParam;

		const FMatrix LocalToWorldMatrix = LocalToWorldTransform.ToMatrixNoScale();
		const FVector ScaleVector = LocalToWorldTransform.GetScale3D();

		FVector UseOrigin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
		FVector CylinderDirection = (bWorld) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };

		const double CylinderOffsetLength = DynamicPixelToWorldScale * Offset;
		const double CylinderLength = DynamicPixelToWorldScale * Length;
		const double CylinderRadius = DynamicPixelToWorldScale * Radius;
		const FVector CylinderCenter = UseOrigin + (CylinderOffsetLength + CylinderLength * 0.5) * CylinderDirection;

		GizmoMath::RayCylinderIntersection<double>(
			CylinderCenter, CylinderDirection, CylinderRadius, CylinderLength,
			RayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			return FInputRayHit(RayParam);
		}
	}
	return FInputRayHit();
}