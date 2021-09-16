// Copyright Epic Games, Inc. All Rights Reserved.
 
#include "EditorGizmos/GizmoBoxObject.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "Materials/MaterialInterface.h"
#include "InputState.h"

void UGizmoBoxObject::Render(IToolsContextRenderAPI* RenderAPI)
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

		const FVector UseDirection = (bWorld) ? UpDirection : FVector{ LocalToWorldMatrix.TransformVector(UpDirection) };
		bool bFlipToCompare = (FVector::DotProduct(ViewDirection, UseDirection) > 0);
		const FVector CompareDirection = (bFlipToCompare) ? -UseDirection : UseDirection;

		// @todo - make this an object property and static constant on the gizmo
		static const float ViewMaxCosAngle = 0.995f;  // ~5 degrees, cos(0.087 radians)
		bVisibleViewDependent = FMath::Abs(FVector::DotProduct(CompareDirection, ViewDirection)) < ViewMaxCosAngle;

		if (bVisibleViewDependent)
		{
			FMatrix UseRotMatrix = (bWorld) ? FMatrix::Identity : FRotationMatrix(LocalToWorldTransform.GetRotation().Rotator());

			// @todo: replace with a call to CalculateLocalPixelToWorldScale instead if possible
			FVector FlattenScale = FVector::OneVector;
			DynamicPixelToWorldScale = GizmoRenderingUtil::CalculateViewDependentScaleAndFlatten(View, UseOrigin, GizmoScale, FlattenScale);

			FVector UniformDimensions = DynamicPixelToWorldScale * Dimensions;
			FVector FlattenDimensions(FlattenScale[0] == 1.0f ? 1.0f : 1.0f / UniformDimensions[0],
									  FlattenScale[1] == 1.0f ? 1.0f : 1.0f / UniformDimensions[1],
									  FlattenScale[2] == 1.0f ? 1.0f : 1.0f / UniformDimensions[2]);

			UMaterialInterface* UseMaterial = Material;
			if (bHovering || bInteracting)
			{
				UseMaterial = CurrentMaterial;
			}
			check(UseMaterial);

			FVector BoxCenter(0, 0, Offset * DynamicPixelToWorldScale);

			FVector ForwardDirection = FVector::CrossProduct(SideDirection, UpDirection);
			FMatrix AxisRotation = FRotationMatrix::MakeFromYZ(ForwardDirection, UpDirection);

			FMatrix BoxToWorld = FScaleMatrix(UniformDimensions) * FTranslationMatrix(BoxCenter) * AxisRotation * UseRotMatrix * FTranslationMatrix(UseOrigin) * FScaleMatrix(FlattenScale);

			DrawBox(PDI, BoxToWorld, FVector(1, 1, 1), UseMaterial->GetRenderProxy(), SDPG_Foreground);
		}
	}
}

FInputRayHit UGizmoBoxObject::LineTraceObject(const FVector RayStart, const FVector RayDirection)
{
	if (bVisible && bVisibleViewDependent)
	{
		/*
		// @todo
		bool bIntersects = false;
		float RayParam = 0.0f;


		const FVector BoxOrigin = Origin + Offset;
		GizmoMath::RayBoxIntersection(
			BoxOrigin, CubeObject->Axis, CubeObject->Radius, CubeObject->Height,
			ClickPos.WorldRay.Origin, ClickPos.WorldRay.Direction,
			bIntersects, RayParam);

		if (bIntersects)
		{
			return FInputRayHit(RayParam);
		}
		*/
	}
	return FInputRayHit();
}
