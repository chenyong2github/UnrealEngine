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
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "MovieSceneTimeHelpers.h"
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

	ConvertXformable( RelativeTransform, UsdPrim, UsdUtils::GetDefaultTimeCode() );

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

bool UnrealToUsd::ConvertXformable( const UMovieScene3DTransformTrack& MovieSceneTrack, pxr::UsdPrim& UsdPrim, const FMovieSceneSequenceTransform& SequenceTransform )
{
	if ( !UsdPrim )
	{
		return false;
	}

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
		const FUsdStageInfo StageInfo( UsdPrim.GetStage() );
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

		int32 ValueIndex = 0;
		for ( const TPair< FFrameNumber, float >& Value : LocationValuesX )
		{
			FFrameTime UsdFrameTime = FFrameRate::TransformTime( Value.Key, Resolution, StageFrameRate );

			FVector Location( LocationValuesX[ ValueIndex ].Value, LocationValuesY[ ValueIndex ].Value, LocationValuesZ[ ValueIndex ].Value );
			FRotator Rotation( RotationValuesY[ ValueIndex ].Value, RotationValuesZ[ ValueIndex ].Value, RotationValuesX[ ValueIndex ].Value );
			FVector Scale( ScaleValuesX[ ValueIndex ].Value, ScaleValuesY[ ValueIndex ].Value, ScaleValuesZ[ ValueIndex ].Value );

			FTransform Transform( Rotation, Location, Scale );
			ConvertXformable( Transform, UsdPrim, UsdFrameTime.AsDecimal() );

			++ValueIndex;
		}
	}

	return true;
}

#endif // #if USE_USD_SDK
