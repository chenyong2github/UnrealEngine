// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimConversion.h"

#include "USDConversionUtils.h"
#include "USDTypesConversion.h"

#include "CineCameraComponent.h"
#include "Components/SceneComponent.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"

#include "USDIncludesEnd.h"

bool UsdToUnreal::ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomXformable& Xformable, USceneComponent& SceneComponent, pxr::UsdTimeCode EvalTime )
{
	if ( !Xformable )
	{
		return false;
	}

	pxr::TfToken UpAxis = UsdUtils::GetUsdStageAxis( Stage );

	// Transform
	pxr::GfMatrix4d UsdMatrix;
	bool bResetXFormStack = false;
	Xformable.GetLocalTransformation( &UsdMatrix, &bResetXFormStack, EvalTime );

	FRotator AdditionalRotation( ForceInit );

	if ( Xformable.GetPrim().IsA< pxr::UsdGeomCamera >() )
	{
		AdditionalRotation = FRotator( -90.f, 0.f, 0.f );
		AdditionalRotation = AdditionalRotation + FRotator( 0.f, -90.f, 0.f );

		UpAxis = pxr::UsdGeomTokens->y; // Cameras are always Y up in USD
	}

	FTransform Transform = UsdToUnreal::ConvertMatrix( UpAxis, UsdMatrix ) * FTransform( AdditionalRotation );
	SceneComponent.SetRelativeTransform( Transform );

	return true;
}

bool UsdToUnreal::ConvertGeomCamera( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomCamera& GeomCamera, UCineCameraComponent& CameraComponent, pxr::UsdTimeCode EvalTime )
{
	CameraComponent.CurrentFocalLength = UsdUtils::GetUsdValue< float >( GeomCamera.GetFocalLengthAttr(), EvalTime );

	CameraComponent.FocusSettings.ManualFocusDistance = UsdUtils::GetUsdValue< float >( GeomCamera.GetFocusDistanceAttr(), EvalTime );

	if ( FMath::IsNearlyZero( CameraComponent.FocusSettings.ManualFocusDistance ) )
	{
		CameraComponent.FocusSettings.FocusMethod = ECameraFocusMethod::None;
	}

	CameraComponent.CurrentAperture = UsdUtils::GetUsdValue< float >( GeomCamera.GetFStopAttr(), EvalTime );

	CameraComponent.Filmback.SensorWidth = UsdUtils::GetUsdValue< float >( GeomCamera.GetHorizontalApertureAttr(), EvalTime );
	CameraComponent.Filmback.SensorHeight = UsdUtils::GetUsdValue< float >( GeomCamera.GetVerticalApertureAttr(), EvalTime );

	return true;
}

bool UnrealToUsd::ConvertSceneComponent( const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim )
{
	if ( !UsdPrim || !SceneComponent )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	// Transform
	pxr::UsdGeomXformable XForm( UsdPrim );
	if ( XForm )
	{
		pxr::GfMatrix4d UsdMatrix;
		bool bResetXFormStack = false;
		XForm.GetLocalTransformation( &UsdMatrix, &bResetXFormStack );

		pxr::GfMatrix4d UsdTransform = UnrealToUsd::ConvertTransform( Stage, SceneComponent->GetRelativeTransform() );

		if ( GfIsClose( UsdMatrix, UsdTransform, THRESH_VECTORS_ARE_NEAR ) )
		{
			return false;
		}

		bResetXFormStack = false;
		bool bFoundTransformOp = false;
	
		std::vector< pxr::UsdGeomXformOp > XFormOps = XForm.GetOrderedXformOps( &bResetXFormStack );
		for ( const pxr::UsdGeomXformOp& XFormOp : XFormOps )
		{
			// Found transform op, trying to set its value
			if ( XFormOp.GetOpType() == pxr::UsdGeomXformOp::TypeTransform )
			{
				bFoundTransformOp = true;
				XFormOp.Set( UsdTransform );
				break;
			}
		}

		// If transformOp is not found, make a new one
		if ( !bFoundTransformOp )
		{
			pxr::UsdGeomXformOp MatrixXform = XForm.MakeMatrixXform();
			if ( MatrixXform )
			{
				MatrixXform.Set( UsdTransform );
			}
		}
	}

	return true;
}
#endif // #if USE_USD_SDK
