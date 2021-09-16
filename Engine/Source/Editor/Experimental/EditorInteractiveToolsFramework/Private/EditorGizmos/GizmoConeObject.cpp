// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "EditorGizmos/GizmoConeObject.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/GizmoMath.h"
#include "InputState.h"
#include "Materials/MaterialInterface.h"

void UGizmoConeObject::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bVisible)
	{
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

			const FVector Base(Offset * DynamicPixelToWorldScale, 0, 0);
			FMatrix AxisRotation = FRotationMatrix::MakeFromX(UseDirection);
			FMatrix ConeToWorld = FScaleMatrix(Height * DynamicPixelToWorldScale) * FTranslationMatrix(Base) * AxisRotation * FTranslationMatrix(UseOrigin) * FScaleMatrix(FlattenScale);
			DrawCone(PDI, ConeToWorld, Angle, Angle, NumSides, false, FColor::White, UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}
}

FInputRayHit UGizmoConeObject::LineTraceObject(const FVector RayOrigin, const FVector RayDirection)
{
	if (bVisible && bVisibleViewDependent)
	{
		bool bIntersects = false;
		double RayParam = 0.0;

		const FMatrix LocalToWorldMatrix = LocalToWorldTransform.ToMatrixNoScale();

		const FVector Origin = LocalToWorldMatrix.TransformPosition(FVector::ZeroVector);
		const FVector ConeDirection = (bWorld) ? Direction : FVector{ LocalToWorldMatrix.TransformVector(Direction) };
		const double ConeHeight = Height * DynamicPixelToWorldScale;
		const double ConeOffset = Offset * DynamicPixelToWorldScale;
		const FVector ConeOrigin = Origin + ConeOffset * ConeDirection;

		GizmoMath::RayConeIntersection(
			ConeOrigin, 
			ConeDirection, 
			FMath::Cos(Angle), 
			ConeHeight,
			RayOrigin, RayDirection,
			bIntersects, RayParam);

		if (bIntersects)
		{
			return FInputRayHit(RayParam);
		}
	}
	return FInputRayHit();
}