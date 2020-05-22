// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDTypesConversion.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usdLux/light.h"

#include "USDIncludesEnd.h"

bool UsdToUnreal::ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdTyped& Schema, FTransform& OutTransform, double EvalTime )
{
	pxr::UsdGeomXformable Xformable( Schema );
	if ( !Xformable )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	// Transform
	pxr::GfMatrix4d UsdMatrix;
	bool bResetXFormStack = false;
	Xformable.GetLocalTransformation( &UsdMatrix, &bResetXFormStack, EvalTime );

	FUsdStageInfo StageInfo( Stage );

	// Extra rotation to match different camera facing direction convention
	// Note: The camera space is always Y-up, yes, but this is not what this is: This is the camera's transform wrt the stage,
	// which follows the stage up axis
	FRotator AdditionalRotation( ForceInit );
	if ( Xformable.GetPrim().IsA< pxr::UsdGeomCamera >() )
	{
		if ( StageInfo.UpAxis == EUsdUpAxis::YAxis )
		{
			AdditionalRotation = FRotator(0.0f, -90.f, 0.0f);
		}
		else
		{
			AdditionalRotation = FRotator(-90.0f, -90.f, 0.0f);
		}
	}
	else if ( Xformable.GetPrim().IsA< pxr::UsdLuxLight >() )
	{
		AdditionalRotation = FRotator( 0.f, -90.f, 0.f ); // UE emits light in +X, USD emits in -Z
	}

	OutTransform = FTransform( AdditionalRotation ) * UsdToUnreal::ConvertMatrix( StageInfo, UsdMatrix );

	return true;
}

bool UsdToUnreal::ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdTyped& Schema, USceneComponent& SceneComponent, double EvalTime )
{
	pxr::UsdGeomXformable Xformable( Schema );
	if ( !Xformable )
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE( UsdToUnreal::ConvertXformable );

	FScopedUsdAllocs UsdAllocs;

	// Transform
	FTransform Transform;
	UsdToUnreal::ConvertXformable( Stage, Xformable, Transform, EvalTime );

	SceneComponent.SetRelativeTransform( Transform );

	// Visibility
	const bool bIsHidden = ( Xformable.ComputeVisibility( EvalTime ) == pxr::UsdGeomTokens->invisible );

	SceneComponent.Modify();
	SceneComponent.SetVisibility( !bIsHidden );

	return true;
}

bool UsdToUnreal::ConvertGeomCamera( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomCamera& GeomCamera, UCineCameraComponent& CameraComponent, double EvalTime )
{
	FUsdStageInfo StageInfo( Stage );

	CameraComponent.CurrentFocalLength = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( GeomCamera.GetFocalLengthAttr(), EvalTime ) );

	CameraComponent.FocusSettings.ManualFocusDistance = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( GeomCamera.GetFocusDistanceAttr(), EvalTime ) );

	if ( FMath::IsNearlyZero( CameraComponent.FocusSettings.ManualFocusDistance ) )
	{
		CameraComponent.FocusSettings.FocusMethod = ECameraFocusMethod::DoNotOverride;
	}

	CameraComponent.CurrentAperture = UsdUtils::GetUsdValue< float >( GeomCamera.GetFStopAttr(), EvalTime );

	CameraComponent.Filmback.SensorWidth = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( GeomCamera.GetHorizontalApertureAttr(), EvalTime ) );
	CameraComponent.Filmback.SensorHeight = UsdToUnreal::ConvertDistance( StageInfo, UsdUtils::GetUsdValue< float >( GeomCamera.GetVerticalApertureAttr(), EvalTime ) );

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
	if ( !XForm )
	{
		return false;
	}

	FTransform RelativeTransform = SceneComponent->GetRelativeTransform();

	FTransform AdditionalRotation;
	if ( ACineCameraActor* CineCameraActor = Cast< ACineCameraActor >( SceneComponent->GetOwner() ) )
	{
		AdditionalRotation = FTransform( FRotator( 0.0f, 90.f, 0.0f ) );

		if ( UsdUtils::GetUsdStageAxis( Stage ) == pxr::UsdGeomTokens->z )
		{
			AdditionalRotation *= FTransform( FRotator( 90.0f, 0.f, 0.0f ) );
		}
	}
	else if ( const ULightComponent* LightComponent = Cast< ULightComponent >( SceneComponent ) )
	{
		const FRotator LightRotation( 0.f, 90.f, 0.f ); // UE emits light in +X, USD emits in -Z
		AdditionalRotation = FTransform( LightRotation );
	}

	RelativeTransform = AdditionalRotation * RelativeTransform;

	FUsdStageInfo StageInfo( Stage );
	pxr::GfMatrix4d UsdTransform = UnrealToUsd::ConvertTransform( StageInfo, RelativeTransform );

	pxr::GfMatrix4d UsdMatrix;
	bool bResetXFormStack = false;
	XForm.GetLocalTransformation( &UsdMatrix, &bResetXFormStack );

	if ( !GfIsClose( UsdMatrix, UsdTransform, THRESH_VECTORS_ARE_NEAR ) )
	{
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

	// Visibility
	bool bVisible = SceneComponent->GetVisibleFlag();
	if ( bVisible )
	{
		XForm.MakeVisible();
	}
	else
	{
		XForm.MakeInvisible();
	}

	return true;
}

bool UnrealToUsd::ConvertMeshComponent( const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, pxr::UsdPrim& UsdPrim )
{
	if ( !UsdPrim || !MeshComponent )
	{
		return false;
	}

	if ( !ConvertSceneComponent( Stage, MeshComponent, UsdPrim ) )
	{
		return false;
	}

	const bool bHasMaterialAttribute = UsdPrim.HasAttribute( UnrealIdentifiers::MaterialAssignments );

	if ( MeshComponent->GetNumMaterials() > 0 || bHasMaterialAttribute )
	{
		FScopedUsdAllocs UsdAllocs;

		pxr::VtArray< std::string > UEMaterials;

		bool bHasUEMaterialAssignements = false;

		for ( int32 MaterialIndex = 0; MaterialIndex < MeshComponent->GetNumMaterials(); ++MaterialIndex )
		{
			if ( UMaterialInterface* AssignedMaterial = MeshComponent->GetMaterial( MaterialIndex ) )
			{
				FString AssignedMaterialPathName;
				if ( AssignedMaterial->GetOutermost() != GetTransientPackage() )
				{
					AssignedMaterialPathName = AssignedMaterial->GetPathName();
					bHasUEMaterialAssignements = true;
				}

				UEMaterials.push_back( UnrealToUsd::ConvertString( *AssignedMaterialPathName ).Get() );
			}
		}

		if ( bHasUEMaterialAssignements )
		{
			if ( pxr::UsdAttribute UEMaterialsAttribute = UsdPrim.CreateAttribute( UnrealIdentifiers::MaterialAssignments, pxr::SdfValueTypeNames->StringArray ) )
			{
				UEMaterialsAttribute.Set( UEMaterials );
			}
		}
		else if ( bHasMaterialAttribute )
		{
			UsdPrim.GetAttribute( UnrealIdentifiers::MaterialAssignments ).Clear();
		}
	}

	return true;
}

#endif // #if USE_USD_SDK
