// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDPrimResolver.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdPrim;
PXR_NAMESPACE_CLOSE_SCOPE
#endif // #if USE_USD_SDK

#include "USDPrimResolverKind.generated.h"

/** Evaluates USD prims based on USD kind metadata */
UCLASS(transient, MinimalAPI)
class UUSDPrimResolverKind : public UUSDPrimResolver
{
	GENERATED_BODY()

#if USE_USD_SDK
public:
	virtual void FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnData) const override;

private:
	void FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& Prim, const TUsdStore< pxr::UsdPrim >& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const override;
#endif // #if USE_USD_SDK
};