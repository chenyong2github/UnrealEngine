// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDConversionUtils.h"

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdAttribute;
	class UsdGeomCamera;
	class UsdGeomXformable;
	class UsdPrim;
	class UsdTimeCode;
	class UsdTyped;

	class UsdStage;
	template< typename T > class TfRefPtr;

	using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

class AInstancedFoliageActor;
class UCineCameraComponent;
class UHierarchicalInstancedStaticMeshComponent;
class ULevel;
class UMeshComponent;
class UMovieScene;
class UMovieScene3DTransformTrack;
class USceneComponent;
class USkeletalMeshComponent;
struct FFrameRate;
struct FMovieSceneSequenceTransform;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdTyped& Schema, USceneComponent& SceneComponent, double EvalTime );
	USDUTILITIES_API bool ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdTyped& Schema, FTransform& OutTransform, double EvalTime );

	/**
	 * Converts a time varying UsdGeomXformable to a UMovieScene3DTransformTrack
	 * @param Schema               The Xformable to read from
	 * @param MovieSceneTrack      The track to add the time sampled transform to
	 * @param SequenceTransform    The time transform to apply to the track keys to get them from Usd Stage time to track time (in other words: from main sequence to subsequence)
	 */
	USDUTILITIES_API bool ConvertXformable( const pxr::UsdTyped& Schema, UMovieScene3DTransformTrack& MovieSceneTrack, const FMovieSceneSequenceTransform& SequenceTransform );

	USDUTILITIES_API bool ConvertGeomCamera( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomCamera& GeomCamera, UCineCameraComponent& CameraComponent, double EvalTime );
}

namespace UnrealToUsd
{
	USDUTILITIES_API bool ConvertSceneComponent( const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim );
	USDUTILITIES_API bool ConvertMeshComponent( const pxr::UsdStageRefPtr& Stage, const UMeshComponent* MeshComponent, pxr::UsdPrim& UsdPrim );
	USDUTILITIES_API bool ConvertHierarchicalInstancedStaticMeshComponent( const UHierarchicalInstancedStaticMeshComponent* HISMComponent, pxr::UsdPrim& UsdPrim, double TimeCode = UsdUtils::GetDefaultTimeCode() );
	USDUTILITIES_API bool ConvertCameraComponent( const pxr::UsdStageRefPtr& Stage, const UCineCameraComponent* CameraComponent, pxr::UsdPrim& UsdPrim, double TimeCode = UsdUtils::GetDefaultTimeCode() );

	USDUTILITIES_API bool ConvertXformable( const FTransform& RelativeTransform, pxr::UsdPrim& UsdPrim, double TimeCode );

	/**
	 * Converts a UMovieScene3DTransformTrack to a UsdGeomXformable
	 * @param MovieSceneTrack      The track to read the time sampled transform from
	 * @param Prim                 The Xformable to write to
	 * @param SequenceTransform    The time transform to apply to the track keys to get them from Usd Stage time to track time (in other words: from main sequence to subsequence)
	 */
	USDUTILITIES_API bool ConvertXformable( const UMovieScene3DTransformTrack& MovieSceneTrack, pxr::UsdPrim& UsdPrim, const FMovieSceneSequenceTransform& SequenceTransform );

	/**
	 * Converts a AInstancedFoliageActor to a prim containing a pxr::UsdGeomPointInstancer schema. Each foliage type should correspond to a prototype.
	 * This function only converts the protoIndices, positions, orientations and scales attributes.
	 *
	 * @param Actor				   The actor to convert data from
	 * @param Prim                 The pxr::UsdGeomPointInstancer to write to
	 * @param TimeCode			   TimeCode to write the attribute values at. Use UsdUtils::GetDefaultTimeCode() for the Default value.
	 */
	USDUTILITIES_API bool ConvertInstancedFoliageActor( const AInstancedFoliageActor& Actor, pxr::UsdPrim& UsdPrim, double TimeCode );

	// We will defer to the common UnrealToUsd::ConvertXComponent functions when baking level sequences, which will already bake
	// all of the properties of a component out at the same time.
	// We use this enum to keep track of which type of baking was already being done for a component, so that if
	// e.g. we have a track for camera aperture and another for camera focal length, we end up just baking that camera
	// only once (as only once will be enough to handle all animated properties)
	enum class USDUTILITIES_API EBakingType : uint8
	{
		None = 0,
		Transform = 1,
		Visibility = 2,
		Camera = 4,
		Light = 8,
		Skeletal = 16,
	};
	ENUM_CLASS_FLAGS( EBakingType );

	// Contains a lambda function responsible for baking a USceneComponent on the level into a prim on an FUsdStage
	struct USDUTILITIES_API FComponentBaker
	{
		EBakingType BakerType;
		TFunction<void( double UsdTimeCode )> BakerFunction;
	};

	/**
	 * Creates a property baker responsible for baking Component's current state onto Prim according to its component type,
	 * if PropertyPath describes a property that we map from UE to USD for that component type.
	 *
	 * Returns whether a baker was created or not.
	 *
	 * e.g. if we provide a UCineCameraComponent and PropertyPath == "FocusSettings.ManualFocusDistance" it will create a
	 * EBakingType::Camera baker (that bakes all of the mappable UE cine camera properties at once) and return true.
	 * If we provide PropertyPath == "FocusSettings.FocusSmoothingInterpSpeed" it will return false as that is not mapped to USD.
	 */
	USDUTILITIES_API bool CreateComponentPropertyBaker( UE::FUsdPrim& Prim, const USceneComponent& Component, const FString& PropertyPath, FComponentBaker& OutBaker );

	/**
	 * Creates a property baker responsible for baking the joint/blend shape state of Component as a SkelAnimation,
	 * returning true if a EBakingType::Skeletal baker was created
	 */
	USDUTILITIES_API bool CreateSkeletalAnimationBaker( UE::FUsdPrim& SkelRoot, UE::FUsdPrim& SkelAnimation, USkeletalMeshComponent& Component, FComponentBaker& OutBaker );
}

#endif // #if USE_USD_SDK
