// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimConversion.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDLayerUtils.h"
#include "USDTypesConversion.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "InstancedFoliageActor.h"
#include "MovieSceneTimeHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/sdf/changeBlock.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/camera.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/pointInstancer.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xformCommonAPI.h"
#include "pxr/usd/usdLux/light.h"
#include "pxr/usd/usdSkel/root.h"

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
			AdditionalRotation = FRotator( 0.0f, -90.f, 0.0f );
		}
		else
		{
			AdditionalRotation = FRotator( -90.0f, -90.f, 0.0f );
		}
	}
	else if ( Xformable.GetPrim().IsA< pxr::UsdLuxLight >() )
	{
		if ( StageInfo.UpAxis == EUsdUpAxis::YAxis )
		{
			AdditionalRotation = FRotator( 0.0f, -90.f, 0.0f );
		}
		else
		{
			AdditionalRotation = FRotator( -90.0f, -90.f, 0.0f );
		}
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

	SceneComponent.Modify();

	// Computed (effective) visibility
	const bool bIsHidden = ( Xformable.ComputeVisibility( EvalTime ) == pxr::UsdGeomTokens->invisible );
	SceneComponent.SetVisibility( !bIsHidden );

	// Per-prim visibility
	bool bIsInvisible = false; // Default to 'inherited'
	if ( pxr::UsdAttribute VisibilityAttr = Xformable.GetVisibilityAttr() )
	{
		pxr::TfToken Value;
		if ( VisibilityAttr.Get( &Value, EvalTime ) )
		{
			bIsInvisible = Value == pxr::UsdGeomTokens->invisible;
		}
	}
	if ( bIsInvisible )
	{
		SceneComponent.ComponentTags.AddUnique( UnrealIdentifiers::Invisible );
		SceneComponent.ComponentTags.Remove( UnrealIdentifiers::Inherited );
	}
	else
	{
		SceneComponent.ComponentTags.Remove( UnrealIdentifiers::Invisible );
		SceneComponent.ComponentTags.AddUnique( UnrealIdentifiers::Inherited );
	}

	return true;
}

bool UsdToUnreal::ConvertXformable( const pxr::UsdTyped& Schema, UMovieScene3DTransformTrack& MovieSceneTrack, const FMovieSceneSequenceTransform& SequenceTransform )
{
	const UMovieScene* MovieScene = MovieSceneTrack.GetTypedOuter< UMovieScene >();

	if ( !MovieScene )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomXformable Xformable( Schema );

	if ( !Xformable )
	{
		return false;
	}

	std::vector< double > UsdTimeSamples;
	Xformable.GetTimeSamples( &UsdTimeSamples );

	if ( UsdTimeSamples.empty() )
	{
		return false;
	}

	const FFrameRate Resolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();

	TArray< FFrameNumber > FrameNumbers;
	FrameNumbers.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > TranslationValuesX;
	TranslationValuesX.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > TranslationValuesY;
	TranslationValuesY.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > TranslationValuesZ;
	TranslationValuesZ.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > RotationValuesX;
	RotationValuesX.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > RotationValuesY;
	RotationValuesY.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > RotationValuesZ;
	RotationValuesZ.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > ScaleValuesX;
	ScaleValuesX.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > ScaleValuesY;
	ScaleValuesY.Reserve( UsdTimeSamples.size() );

	TArray< FMovieSceneFloatValue > ScaleValuesZ;
	ScaleValuesZ.Reserve( UsdTimeSamples.size() );

	pxr::UsdStageRefPtr Stage = Schema.GetPrim().GetStage();
	const double StageTimeCodesPerSecond = Stage->GetTimeCodesPerSecond();
	const FFrameRate StageFrameRate( StageTimeCodesPerSecond, 1 );

	const ERichCurveInterpMode InterpMode = ( Stage->GetInterpolationType() == pxr::UsdInterpolationTypeLinear ) ? ERichCurveInterpMode::RCIM_Linear : ERichCurveInterpMode::RCIM_Constant;

	for ( double UsdTimeSample : UsdTimeSamples )
	{
		// Frame Number
		int32 FrameNumber = FMath::FloorToInt( UsdTimeSample );
		float SubFrameNumber = UsdTimeSample - FrameNumber;

		FFrameTime FrameTime( FrameNumber, SubFrameNumber );

		FFrameTime KeyFrameTime = FFrameRate::TransformTime( FrameTime, StageFrameRate, Resolution );
		KeyFrameTime *= SequenceTransform;
		FrameNumbers.Add( KeyFrameTime.GetFrame() );

		// Frame Values
		FTransform Transform;
		UsdToUnreal::ConvertXformable( Stage, Xformable, Transform, UsdTimeSample );

		// Location
		TranslationValuesX.Emplace_GetRef( Transform.GetLocation().X ).InterpMode = InterpMode;
		TranslationValuesY.Emplace_GetRef( Transform.GetLocation().Y ).InterpMode = InterpMode;
		TranslationValuesZ.Emplace_GetRef( Transform.GetLocation().Z ).InterpMode = InterpMode;

		// Rotation
		FRotator Rotator = Transform.Rotator();
		RotationValuesX.Emplace_GetRef( Rotator.Roll ).InterpMode = InterpMode;
		RotationValuesY.Emplace_GetRef( Rotator.Pitch ).InterpMode = InterpMode;
		RotationValuesZ.Emplace_GetRef( Rotator.Yaw ).InterpMode = InterpMode;

		// Scale
		ScaleValuesX.Emplace_GetRef( Transform.GetScale3D().X ).InterpMode = InterpMode;
		ScaleValuesY.Emplace_GetRef( Transform.GetScale3D().Y ).InterpMode = InterpMode;
		ScaleValuesZ.Emplace_GetRef( Transform.GetScale3D().Z ).InterpMode = InterpMode;
	}

	bool bSectionAdded = false;
	UMovieScene3DTransformSection* TransformSection = Cast< UMovieScene3DTransformSection >( MovieSceneTrack.FindOrAddSection( 0, bSectionAdded ) );
	TransformSection->EvalOptions.CompletionMode = EMovieSceneCompletionMode::KeepState;
	TransformSection->SetRange( TRange< FFrameNumber >::All() );

	TArrayView< FMovieSceneFloatChannel* > Channels = TransformSection->GetChannelProxy().GetChannels< FMovieSceneFloatChannel >();

	check( Channels.Num() >= 9 );

	// Translation
	Channels[0]->Set( FrameNumbers, TranslationValuesX );
	Channels[1]->Set( FrameNumbers, TranslationValuesY );
	Channels[2]->Set( FrameNumbers, TranslationValuesZ );

	// Rotation
	Channels[3]->Set( FrameNumbers, RotationValuesX );
	Channels[4]->Set( FrameNumbers, RotationValuesY );
	Channels[5]->Set( FrameNumbers, RotationValuesZ );

	// Scale
	Channels[6]->Set( FrameNumbers, ScaleValuesX );
	Channels[7]->Set( FrameNumbers, ScaleValuesY );
	Channels[8]->Set( FrameNumbers, ScaleValuesZ );

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

	// Compensate different orientation for light or camera components :
	// In USD cameras shoot towards local - Z, with + Y up.Lights also emit towards local - Z, with + Y up
	// In UE cameras shoot towards local + X, with + Z up.Lights also emit towards local + X, with + Z up
	if ( SceneComponent->IsA( UCineCameraComponent::StaticClass() ) ||
		 SceneComponent->IsA( ULightComponent::StaticClass() ) )
	{
		FTransform AdditionalRotation = FTransform( FRotator( 0.0f, 90.f, 0.0f ) );

		if ( UsdUtils::GetUsdStageUpAxis( Stage ) == pxr::UsdGeomTokens->z )
		{
			AdditionalRotation *= FTransform( FRotator( 90.0f, 0.f, 0.0f ) );
		}

		RelativeTransform = AdditionalRotation * RelativeTransform;
	}

	// Invert compensation applied to parent if it's a light or camera component
	if ( USceneComponent* AttachParent = SceneComponent->GetAttachParent() )
	{
		if ( AttachParent->IsA( UCineCameraComponent::StaticClass() ) ||
			 AttachParent->IsA( ULightComponent::StaticClass() ) )
		{
			FTransform AdditionalRotation = FTransform( FRotator( 0.0f, 90.f, 0.0f ) );

			if ( UsdUtils::GetUsdStageUpAxis( Stage ) == pxr::UsdGeomTokens->z )
			{
				AdditionalRotation *= FTransform( FRotator( 90.0f, 0.f, 0.0f ) );
			}

			RelativeTransform = RelativeTransform * AdditionalRotation.Inverse();
		}
	}

	// Transform
	ConvertXformable( RelativeTransform, UsdPrim, UsdUtils::GetDefaultTimeCode() );

	// Per-prim visibility
	if ( pxr::UsdAttribute VisibilityAttr = XForm.CreateVisibilityAttr() )
	{
		pxr::TfToken Value = pxr::UsdGeomTokens->inherited;

		if ( SceneComponent->ComponentTags.Contains( UnrealIdentifiers::Invisible ) )
		{
			Value = pxr::UsdGeomTokens->invisible;
		}
		else if ( !SceneComponent->ComponentTags.Contains( UnrealIdentifiers::Inherited ) )
		{
			// We don't have visible nor inherited tags: We're probably exporting a pure UE component, so write out component visibility instead
			Value = SceneComponent->IsVisible() ? pxr::UsdGeomTokens->inherited : pxr::UsdGeomTokens->invisible;
		}

		VisibilityAttr.Set<pxr::TfToken>( Value );
	}

	return true;
}

bool UnrealToUsd::ConvertMeshComponent( const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, pxr::UsdPrim& UsdPrim )
{
	if ( !UsdPrim || !MeshComponent )
	{
		return false;
	}

	FScopedUsdAllocs Allocs;

	if ( const UStaticMeshComponent* StaticMeshComponent = Cast<const UStaticMeshComponent>( MeshComponent ) )
	{
#if WITH_EDITOR
		if ( UStaticMesh* Mesh = StaticMeshComponent->GetStaticMesh() )
		{
			int32 NumLODs = Mesh->GetNumLODs();
			const bool bHasLODs = NumLODs > 1;

			const TArray<UMaterialInterface*>& Overrides = MeshComponent->OverrideMaterials;
			for ( int32 MatIndex = 0; MatIndex < Overrides.Num(); ++MatIndex )
			{
				UMaterialInterface* Override = Overrides[MatIndex];
				if ( !Override )
				{
					continue;
				}

				for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
				{
					int32 NumSections = Mesh->GetNumSections( LODIndex );
					const bool bHasSubsets = NumSections > 1;

					for ( int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex )
					{
						int32 SectionMatIndex = Mesh->GetSectionInfoMap().Get( LODIndex, SectionIndex ).MaterialIndex;
						if ( SectionMatIndex != MatIndex )
						{
							continue;
						}

						pxr::SdfPath OverridePrimPath = UsdPrim.GetPath();

						// If we have only 1 LOD, the asset's DefaultPrim will be the Mesh prim directly.
						// If we have multiple, the default prim won't have any schema, but will contain separate
						// Mesh prims for each LOD named "LOD0", "LOD1", etc., switched via a "LOD" variant set
						if ( bHasLODs )
						{
							OverridePrimPath = OverridePrimPath.AppendPath( UnrealToUsd::ConvertPath( *FString::Printf( TEXT( "LOD%d" ), LODIndex ) ).Get() );
						}

						// If our LOD has only one section, its material assignment will be authored directly on the Mesh prim.
						// If it has more than one material slot, we'll author UsdGeomSubset for each LOD Section, and author the material
						// assignment there
						if ( bHasSubsets )
						{
							OverridePrimPath = OverridePrimPath.AppendPath( UnrealToUsd::ConvertPath( *FString::Printf( TEXT( "Section%d" ), SectionIndex ) ).Get() );
						}

						pxr::UsdPrim OverridePrim = Stage->OverridePrim( OverridePrimPath );
						if ( pxr::UsdAttribute UnrealMaterialAttr = OverridePrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
						{
							UnrealMaterialAttr.Set( UnrealToUsd::ConvertString( *Override->GetPathName() ).Get() );
						}
					}
				}
			}
		}
#endif // WITH_EDITOR
	}
	else if ( const USkinnedMeshComponent* SkinnedMeshComponent = Cast<const USkinnedMeshComponent>( MeshComponent ) )
	{
		pxr::UsdSkelRoot SkelRoot{ UsdPrim };
		if ( !SkelRoot )
		{
			return false;
		}

		if ( const USkeletalMesh* SkeletalMesh = SkinnedMeshComponent->SkeletalMesh )
		{
			FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
			if ( !RenderData )
			{
				return false;
			}

			TIndirectArray<FSkeletalMeshLODRenderData>& LodRenderData = RenderData->LODRenderData;
			if ( LodRenderData.Num() == 0 )
			{
				return false;
			}

			int32 NumLODs = SkeletalMesh->GetLODNum();
			const bool bHasLODs = NumLODs > 1;

			FString MeshName;
			if ( !bHasLODs )
			{
				for ( const pxr::UsdPrim& Child : UsdPrim.GetChildren() )
				{
					if ( pxr::UsdGeomMesh Mesh{ Child } )
					{
						MeshName = UsdToUnreal::ConvertToken( Child.GetName() );
					}
				}
			}

			const TArray<UMaterialInterface*>& Overrides = MeshComponent->OverrideMaterials;
			for ( int32 MatIndex = 0; MatIndex < Overrides.Num(); ++MatIndex )
			{
				UMaterialInterface* Override = Overrides[ MatIndex ];
				if ( !Override )
				{
					continue;
				}

				for ( int32 LODIndex = 0; LODIndex < NumLODs; ++LODIndex )
				{
					if ( !LodRenderData.IsValidIndex( LODIndex ) )
					{
						continue;
					}

					const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo( LODIndex );

					const TArray<FSkelMeshRenderSection>& Sections = LodRenderData[ LODIndex ].RenderSections;
					int32 NumSections = Sections.Num();
					const bool bHasSubsets = NumSections > 1;

					for ( int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex )
					{
						int32 SectionMatIndex = Sections[ SectionIndex ].MaterialIndex;

						// If we have a LODInfo map, we need to reroute the material index through it
						if ( LODInfo && LODInfo->LODMaterialMap.IsValidIndex( SectionIndex ) )
						{
							SectionMatIndex = LODInfo->LODMaterialMap[ SectionIndex ];
						}

						if ( SectionMatIndex != MatIndex )
						{
							continue;
						}

						pxr::SdfPath OverridePrimPath = UsdPrim.GetPath();

						// If we have only 1 LOD, the asset's DefaultPrim will be a SkelRoot, and the Mesh will be a subprim
						// with the same name.If we have the default prim is also the SkelRoot, but will contain separate
						// Mesh prims for each LOD named "LOD0", "LOD1", etc., switched via a "LOD" variant set
						if ( bHasLODs )
						{
							OverridePrimPath = OverridePrimPath.AppendPath( UnrealToUsd::ConvertPath( *FString::Printf( TEXT( "LOD%d" ), LODIndex ) ).Get() );
						}
						else
						{
							OverridePrimPath = OverridePrimPath.AppendElementString( UnrealToUsd::ConvertString( *MeshName ).Get() );
						}

						// If our LOD has only one section, its material assignment will be authored directly on the Mesh prim.
						// If it has more than one material slot, we'll author UsdGeomSubset for each LOD Section, and author the material
						// assignment there
						if ( bHasSubsets )
						{
							OverridePrimPath = OverridePrimPath.AppendPath( UnrealToUsd::ConvertPath( *FString::Printf( TEXT( "Section%d" ), SectionIndex ) ).Get() );
						}

						if ( pxr::UsdPrim OverridePrim = Stage->OverridePrim( OverridePrimPath ) )
						{
							if ( pxr::UsdAttribute UnrealMaterialAttr = OverridePrim.CreateAttribute( UnrealIdentifiers::MaterialAssignment, pxr::SdfValueTypeNames->String ) )
							{
								UnrealMaterialAttr.Set( UnrealToUsd::ConvertString( *Override->GetPathName() ).Get() );
							}
						}
					}
				}
			}
		}
	}

	return true;
}

bool UnrealToUsd::ConvertHierarchicalInstancedStaticMeshComponent( const UHierarchicalInstancedStaticMeshComponent* HISMComponent, pxr::UsdPrim& UsdPrim, double TimeCode )
{
	using namespace pxr;

	FScopedUsdAllocs Allocs;

	UsdGeomPointInstancer PointInstancer{ UsdPrim };
	if ( !PointInstancer || !HISMComponent )
	{
		return false;
	}

	UsdStageRefPtr Stage = UsdPrim.GetStage();
	FUsdStageInfo StageInfo{ Stage };

	VtArray<int> ProtoIndices;
	VtArray<GfVec3f> Positions;
	VtArray<GfQuath> Orientations;
	VtArray<GfVec3f> Scales;

	const int32 NumInstances = HISMComponent->GetInstanceCount();
	ProtoIndices.reserve( ProtoIndices.size() + NumInstances );
	Positions.reserve( Positions.size() + NumInstances );
	Orientations.reserve( Orientations.size() + NumInstances );
	Scales.reserve( Scales.size() + NumInstances );

	for( const FInstancedStaticMeshInstanceData& InstanceData : HISMComponent->PerInstanceSMData )
	{
		// Convert axes
		FTransform UETransform{ InstanceData.Transform };
		FTransform USDTransform = UsdUtils::ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, UETransform );

		FVector Translation = USDTransform.GetTranslation();
		FQuat Rotation = USDTransform.GetRotation();
		FVector Scale = USDTransform.GetScale3D();

		// Compensate metersPerUnit
		const float UEMetersPerUnit = 0.01f;
		if ( !FMath::IsNearlyEqual( UEMetersPerUnit, StageInfo.MetersPerUnit ) )
		{
			Translation *= ( UEMetersPerUnit / StageInfo.MetersPerUnit );
		}

		ProtoIndices.push_back( 0 ); // We will always export a single prototype per PointInstancer, since HISM components handle only 1 mesh at a time
		Positions.push_back( GfVec3f( Translation.X, Translation.Y, Translation.Z ) );
		Orientations.push_back( GfQuath( Rotation.W, Rotation.X, Rotation.Y, Rotation.Z ) );
		Scales.push_back( GfVec3f( Scale.X, Scale.Y, Scale.Z ) );
	}

	const pxr::UsdTimeCode UsdTimeCode( TimeCode );

	if ( UsdAttribute Attr = PointInstancer.CreateProtoIndicesAttr() )
	{
		Attr.Set( ProtoIndices, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreatePositionsAttr() )
	{
		Attr.Set( Positions, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateOrientationsAttr() )
	{
		Attr.Set( Orientations, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateScalesAttr() )
	{
		Attr.Set( Scales, UsdTimeCode );
	}

	return true;
}

bool UnrealToUsd::ConvertCameraComponent( const pxr::UsdStageRefPtr& Stage, const UCineCameraComponent* CameraComponent, pxr::UsdPrim& UsdPrim )
{
	if ( !UsdPrim || !CameraComponent )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomCamera GeomCamera( UsdPrim );
	if ( !GeomCamera )
	{
		return false;
	}

	FUsdStageInfo StageInfo( UsdPrim.GetStage() );

	if ( pxr::UsdAttribute Attr = GeomCamera.CreateFocalLengthAttr() )
	{
		Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, CameraComponent->CurrentFocalLength ) );
	}

	if ( pxr::UsdAttribute Attr = GeomCamera.CreateFocusDistanceAttr() )
	{
		Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, CameraComponent->FocusSettings.ManualFocusDistance ) );
	}

	if ( pxr::UsdAttribute Attr = GeomCamera.CreateFStopAttr() )
	{
		Attr.Set<float>( CameraComponent->CurrentAperture );
	}

	if ( pxr::UsdAttribute Attr = GeomCamera.CreateHorizontalApertureAttr() )
	{
		Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, CameraComponent->Filmback.SensorWidth ) );
	}

	if ( pxr::UsdAttribute Attr = GeomCamera.CreateVerticalApertureAttr() )
	{
		Attr.Set<float>( UnrealToUsd::ConvertDistance( StageInfo, CameraComponent->Filmback.SensorHeight ) );
	}

	return true;
}

bool UnrealToUsd::ConvertXformable( const FTransform& RelativeTransform, pxr::UsdPrim& UsdPrim, double TimeCode )
{
	if ( !UsdPrim )
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

	FUsdStageInfo StageInfo( UsdPrim.GetStage() );
	pxr::GfMatrix4d UsdTransform = UnrealToUsd::ConvertTransform( StageInfo, RelativeTransform );

	const pxr::UsdTimeCode UsdTimeCode( TimeCode );

	pxr::GfMatrix4d UsdMatrix;
	bool bResetXFormStack = false;
	XForm.GetLocalTransformation( &UsdMatrix, &bResetXFormStack, UsdTimeCode );

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
				XFormOp.Set( UsdTransform, UsdTimeCode );
				break;
			}
		}

		// If transformOp is not found, make a new one
		if ( !bFoundTransformOp )
		{
			pxr::UsdGeomXformOp MatrixXform = XForm.MakeMatrixXform();
			if ( MatrixXform )
			{
				MatrixXform.Set( UsdTransform, UsdTimeCode );
			}
		}
	}

	return true;
}

bool UnrealToUsd::ConvertInstancedFoliageActor( const AInstancedFoliageActor& Actor, pxr::UsdPrim& UsdPrim, double TimeCode )
{
#if WITH_EDITOR
	using namespace pxr;

	FScopedUsdAllocs Allocs;

	UsdGeomPointInstancer PointInstancer{ UsdPrim };
	if ( !PointInstancer )
	{
		return false;
	}

	UsdStageRefPtr Stage = UsdPrim.GetStage();
	FUsdStageInfo StageInfo{ Stage };

	VtArray<int> ProtoIndices;
	VtArray<GfVec3f> Positions;
	VtArray<GfQuath> Orientations;
	VtArray<GfVec3f> Scales;

	int PrototypeIndex = 0;
	for ( const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& FoliagePair : Actor.FoliageInfos )
	{
		const UFoliageType* FoliageType = FoliagePair.Key;
		const FFoliageInfo& Info = FoliagePair.Value.Get();

		for ( const TPair<FFoliageInstanceBaseId, TSet<int32>>& Pair : Info.ComponentHash )
		{
			const TSet<int32>& InstanceSet = Pair.Value;

			const int32 NumInstances = InstanceSet.Num();
			ProtoIndices.reserve( ProtoIndices.size() + NumInstances );
			Positions.reserve( Positions.size() + NumInstances );
			Orientations.reserve( Orientations.size() + NumInstances );
			Scales.reserve( Scales.size() + NumInstances );

			for ( int32 InstanceIndex : InstanceSet )
			{
				const FFoliageInstancePlacementInfo* Instance = &Info.Instances[ InstanceIndex ];

				// Convert axes
				FTransform UETransform{ Instance->Rotation, Instance->Location, Instance->DrawScale3D };
				FTransform USDTransform = UsdUtils::ConvertAxes( StageInfo.UpAxis == EUsdUpAxis::ZAxis, UETransform );

				FVector Translation = USDTransform.GetTranslation();
				FQuat Rotation = USDTransform.GetRotation();
				FVector Scale = USDTransform.GetScale3D();

				// Compensate metersPerUnit
				const float UEMetersPerUnit = 0.01f;
				if ( !FMath::IsNearlyEqual( UEMetersPerUnit, StageInfo.MetersPerUnit ) )
				{
					Translation *= ( UEMetersPerUnit / StageInfo.MetersPerUnit );
				}

				ProtoIndices.push_back( PrototypeIndex );
				Positions.push_back( GfVec3f( Translation.X, Translation.Y, Translation.Z ) );
				Orientations.push_back( GfQuath( Rotation.W, Rotation.X, Rotation.Y, Rotation.Z ) );
				Scales.push_back( GfVec3f( Scale.X, Scale.Y, Scale.Z ) );
			}
		}

		++PrototypeIndex;
	}

	const pxr::UsdTimeCode UsdTimeCode( TimeCode );

	if ( UsdAttribute Attr = PointInstancer.CreateProtoIndicesAttr() )
	{
		Attr.Set( ProtoIndices, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreatePositionsAttr() )
	{
		Attr.Set( Positions, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateOrientationsAttr() )
	{
		Attr.Set( Orientations, UsdTimeCode );
	}

	if ( UsdAttribute Attr = PointInstancer.CreateScalesAttr() )
	{
		Attr.Set( Scales, UsdTimeCode );
	}

	return true;
#else
	return false;
#endif // WITH_EDITOR
}

bool UnrealToUsd::ConvertXformable( const UMovieScene3DTransformTrack& MovieSceneTrack, pxr::UsdPrim& UsdPrim, const FMovieSceneSequenceTransform& SequenceTransform )
{
	if ( !UsdPrim )
	{
		return false;
	}

	const FUsdStageInfo StageInfo( UsdPrim.GetStage() );

	const UMovieScene* MovieScene = MovieSceneTrack.GetTypedOuter< UMovieScene >();
	if ( !MovieScene )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdGeomXformable Xformable( UsdPrim );
	if ( !Xformable )
	{
		return false;
	}

	UMovieScene3DTransformSection* TransformSection = Cast< UMovieScene3DTransformSection >( const_cast< UMovieScene3DTransformTrack& >( MovieSceneTrack ).FindSection( 0 ) );
	if ( !TransformSection )
	{
		return false;
	}

	const TRange< FFrameNumber > PlaybackRange = MovieScene->GetPlaybackRange();
	const FFrameRate Resolution = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();

	const double StageTimeCodesPerSecond = UsdPrim.GetStage()->GetTimeCodesPerSecond();
	const FFrameRate StageFrameRate( StageTimeCodesPerSecond, 1 );

	auto EvaluateChannel = [ &PlaybackRange, &Resolution, &DisplayRate, &SequenceTransform ]( const FMovieSceneFloatChannel* Channel, float DefaultValue ) -> TArray< TPair< FFrameNumber, float > >
	{
		TArray< TPair< FFrameNumber, float > > Values;

		if ( PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound() )
		{
			const FFrameTime Interval = FFrameRate::TransformTime( 1, DisplayRate, Resolution );
			const FFrameNumber StartFrame = UE::MovieScene::DiscreteInclusiveLower( PlaybackRange );
			const FFrameNumber EndFrame = UE::MovieScene::DiscreteExclusiveUpper( PlaybackRange );

			for ( FFrameTime EvalTime = StartFrame; EvalTime < EndFrame; EvalTime += Interval )
			{
				FFrameNumber KeyTime = FFrameRate::Snap( EvalTime, Resolution, DisplayRate ).FloorToFrame();

				float Result = DefaultValue;
				if ( Channel )
				{
					Result = Channel->GetDefault().Get( DefaultValue );
					Channel->Evaluate( KeyTime, Result );
				}

				FFrameTime GlobalEvalTime( KeyTime );
				GlobalEvalTime *= SequenceTransform.InverseLinearOnly();

				Values.Emplace( GlobalEvalTime.GetFrame(), Result );
			}
		}

		return Values;
	};

	TArrayView< FMovieSceneFloatChannel* > Channels = TransformSection->GetChannelProxy().GetChannels< FMovieSceneFloatChannel >();
	check( Channels.Num() >= 9 );

	auto GetChannel = [ &Channels ]( const int32 ChannelIndex ) -> const FMovieSceneFloatChannel*
	{
		if ( Channels.IsValidIndex( ChannelIndex ) )
		{
			return Channels[ ChannelIndex ];
		}
		else
		{
			return  nullptr;
		}
	};

	// Translation
	TArray< TPair< FFrameNumber, float > > LocationValuesX = EvaluateChannel( GetChannel(0), 0.f );
	TArray< TPair< FFrameNumber, float > > LocationValuesY = EvaluateChannel( GetChannel(1), 0.f );
	TArray< TPair< FFrameNumber, float > > LocationValuesZ = EvaluateChannel( GetChannel(2), 0.f );

	// Rotation
	TArray< TPair< FFrameNumber, float > > RotationValuesX = EvaluateChannel( GetChannel(3), 0.f );
	TArray< TPair< FFrameNumber, float > > RotationValuesY = EvaluateChannel( GetChannel(4), 0.f );
	TArray< TPair< FFrameNumber, float > > RotationValuesZ = EvaluateChannel( GetChannel(5), 0.f );

	// Scale
	TArray< TPair< FFrameNumber, float > > ScaleValuesX = EvaluateChannel( GetChannel(6), 1.f );
	TArray< TPair< FFrameNumber, float > > ScaleValuesY = EvaluateChannel( GetChannel(7), 1.f );
	TArray< TPair< FFrameNumber, float > > ScaleValuesZ = EvaluateChannel( GetChannel(8), 1.f );

	bool bIsDataOutOfSync = false;
	{
		int32 ValueIndex = 0;

		FFrameTime UsdStartTime = FFrameRate::TransformTime( PlaybackRange.GetLowerBoundValue(), Resolution, StageFrameRate );
		FFrameTime UsdEndTime = FFrameRate::TransformTime( PlaybackRange.GetUpperBoundValue(), Resolution, StageFrameRate );

		if ( LocationValuesX.Num() > 0 || Xformable.TransformMightBeTimeVarying() )
		{
			std::vector< double > UsdTimeSamples;
			Xformable.GetTimeSamples( &UsdTimeSamples );

			bIsDataOutOfSync = ( UsdTimeSamples.size() != LocationValuesX.Num() );

			if ( !bIsDataOutOfSync )
			{
				for ( const TPair< FFrameNumber, float >& Value : LocationValuesX )
				{
					FFrameTime UsdFrameTime = FFrameRate::TransformTime( Value.Key, Resolution, StageFrameRate );

					FVector Location( LocationValuesX[ ValueIndex ].Value, LocationValuesY[ ValueIndex ].Value, LocationValuesZ[ ValueIndex ].Value );
					FRotator Rotation( RotationValuesY[ ValueIndex ].Value, RotationValuesZ[ ValueIndex ].Value, RotationValuesX[ ValueIndex ].Value );
					FVector Scale( ScaleValuesX[ ValueIndex ].Value, ScaleValuesY[ ValueIndex ].Value, ScaleValuesZ[ ValueIndex ].Value );

					FTransform Transform( Rotation, Location, Scale );
					pxr::GfMatrix4d UsdTransform = UnrealToUsd::ConvertTransform( StageInfo, Transform );

					pxr::GfMatrix4d UsdMatrix;
					bool bResetXFormStack = false;
					Xformable.GetLocalTransformation( &UsdMatrix, &bResetXFormStack, UsdFrameTime.AsDecimal() );

					if ( !pxr::GfIsClose( UsdMatrix, UsdTransform, THRESH_POINTS_ARE_NEAR ) )
					{
						bIsDataOutOfSync = true;
						break;
					}

					++ValueIndex;
				}
			}
		}
	}

	if ( bIsDataOutOfSync )
	{
		if ( pxr::UsdGeomXformOp TransformOp = Xformable.MakeMatrixXform() )
		{
			TransformOp.GetAttr().Clear(); // Clear existing transform data
		}

		pxr::SdfChangeBlock ChangeBlock;

		// Compensate different orientation for light or camera components
		// TODO: Handle inverse compensation when the bound component is a child of a light/camera component
		FTransform AdditionalRotation = FTransform::Identity;
		if ( UsdPrim.IsA< pxr::UsdGeomCamera >() || UsdPrim.IsA< pxr::UsdLuxLight >() )
		{
			AdditionalRotation = FTransform( FRotator( 0.0f, 90.0f, 0.0f ) );

			if ( StageInfo.UpAxis == EUsdUpAxis::ZAxis )
			{
				AdditionalRotation *= FTransform( FRotator( 90.0f, 0.0f, 0.0f ) );
			}
		}

		int32 ValueIndex = 0;
		for ( const TPair< FFrameNumber, float >& Value : LocationValuesX )
		{
			FFrameTime UsdFrameTime = FFrameRate::TransformTime( Value.Key, Resolution, StageFrameRate );

			FVector Location( LocationValuesX[ ValueIndex ].Value, LocationValuesY[ ValueIndex ].Value, LocationValuesZ[ ValueIndex ].Value );
			FRotator Rotation( RotationValuesY[ ValueIndex ].Value, RotationValuesZ[ ValueIndex ].Value, RotationValuesX[ ValueIndex ].Value );
			FVector Scale( ScaleValuesX[ ValueIndex ].Value, ScaleValuesY[ ValueIndex ].Value, ScaleValuesZ[ ValueIndex ].Value );

			FTransform Transform( Rotation, Location, Scale );
			ConvertXformable( AdditionalRotation* Transform, UsdPrim, UsdFrameTime.AsDecimal() );

			++ValueIndex;
		}
	}

	return true;
}

#endif // #if USE_USD_SDK
