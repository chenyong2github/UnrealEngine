// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDPrimResolver.h"

namespace UE
{
	class FUsdPrim;
}

#include "USDPrimResolverKind.generated.h"

/** Evaluates USD prims based on USD kind metadata */
UCLASS(transient, MinimalAPI, Deprecated)
class UDEPRECATED_UUSDPrimResolverKind : public UDEPRECATED_UUSDPrimResolver
{
	GENERATED_BODY()

#if USE_USD_SDK
public:
	virtual void FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnData) const override;

private:
	void FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const UE::FUsdPrim& Prim, const UE::FUsdPrim& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const override;
#endif // #if USE_USD_SDK
};