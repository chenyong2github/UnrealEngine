// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/timeCode.h"

#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomCamera;
	class UsdGeomXformable;
	class UsdPrim;
PXR_NAMESPACE_CLOSE_SCOPE

class UCineCameraComponent;
class USceneComponent;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertXformable( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomXformable& Xformable, USceneComponent& SceneComponent, pxr::UsdTimeCode EvalTime = pxr::UsdTimeCode::EarliestTime() );
	USDUTILITIES_API bool ConvertGeomCamera( const pxr::UsdStageRefPtr& Stage, const pxr::UsdGeomCamera& GeomCamera, UCineCameraComponent& CameraComponent, pxr::UsdTimeCode EvalTime = pxr::UsdTimeCode::EarliestTime() );
}

namespace UnrealToUsd
{
	USDUTILITIES_API bool ConvertSceneComponent( const pxr::UsdStageRefPtr& Stage, const USceneComponent* SceneComponent, pxr::UsdPrim& UsdPrim );
}

#endif // #if USE_USD_SDK
