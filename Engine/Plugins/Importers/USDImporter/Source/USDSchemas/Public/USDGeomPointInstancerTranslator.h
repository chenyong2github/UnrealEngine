// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomMeshTranslator.h"
#include "USDGeomXformableTranslator.h"

#if USE_USD_SDK

struct FUsdSchemaTranslationContext;

class FUsdGeomPointInstancerCreateAssetsTaskChain : public FBuildStaticMeshTaskChain
{
public:
	explicit FUsdGeomPointInstancerCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const UE::FSdfPath& InPrimPath );

protected:
	virtual void SetupTasks() override;
};

class USDSCHEMAS_API FUsdGeomPointInstancerTranslator : public FUsdGeomXformableTranslator
{
public:
	using Super = FUsdGeomXformableTranslator;
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	virtual void CreateAssets() override;
	virtual USceneComponent* CreateComponents() override;
	virtual void UpdateComponents( USceneComponent* SceneComponent ) override;

	virtual bool CollapsesChildren( ECollapsingType CollapsingType ) const override { return true; }
	virtual bool CanBeCollapsed( ECollapsingType CollapsingType ) const override { return false; }
};

#endif // #if USE_USD_SDK
