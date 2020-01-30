// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDSchemaTranslator.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

class USceneComponent;

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomXformable;
PXR_NAMESPACE_CLOSE_SCOPE

class USDSCHEMAS_API FUsdGeomXformableTranslator : public FUsdSchemaTranslator
{
public:
	using FUsdSchemaTranslator::FUsdSchemaTranslator;

	explicit FUsdGeomXformableTranslator( TSubclassOf< USceneComponent > InComponentTypeOverride, TSharedRef< FUsdSchemaTranslationContext > InContext, const pxr::UsdTyped& InSchema );

	virtual USceneComponent* CreateComponents() override;
	virtual void UpdateComponents( USceneComponent* SceneComponent );

protected:
	USceneComponent* CreateComponents( TSubclassOf< USceneComponent > ComponentType );
	USceneComponent* CreateComponents( TSubclassOf< USceneComponent > ComponentType, const bool bNeedsActor );

private:
	TOptional< TSubclassOf< USceneComponent > > ComponentTypeOverride;
};

#endif // #if USE_USD_SDK
