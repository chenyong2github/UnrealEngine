// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

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

struct FFrameRate;
struct FMovieSceneSequenceTransform;
class UCineCameraComponent;
class UMeshComponent;
class UMovieScene;
class UMovieScene3DTransformTrack;
class USceneComponent;

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

	USDUTILITIES_API bool ConvertXformable( const FTransform& RelativeTransform, pxr::UsdPrim& UsdPrim, double TimeCode );

	/**
	 * Converts a UMovieScene3DTransformTrack to a UsdGeomXformable
	 * @param MovieSceneTrack      The track to read the time sampled transform from
	 * @param Prim                 The Xformable to write to
	 * @param SequenceTransform    The time transform to apply to the track keys to get them from Usd Stage time to track time (in other words: from main sequence to subsequence)
	 */
	USDUTILITIES_API bool ConvertXformable( const UMovieScene3DTransformTrack& MovieSceneTrack, pxr::UsdPrim& UsdPrim, const FMovieSceneSequenceTransform& SequenceTransform );
}

#endif // #if USE_USD_SDK
