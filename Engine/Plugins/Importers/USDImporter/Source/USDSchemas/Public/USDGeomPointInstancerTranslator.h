// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomXformableTranslator.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/usd/usdGeom/pointInstancer.h"
#include "USDIncludesEnd.h"

struct FUsdSchemaTranslationContext;

class USDSCHEMAS_API FUsdGeomPointInstancerTranslator : public FUsdGeomXformableTranslator
{
public:
	using Super = FUsdGeomXformableTranslator;
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	virtual void UpdateComponents( USceneComponent* SceneComponent ) override;
	virtual bool CollapsesChildren( ECollapsingType CollapsingType ) const override { return CollapsingType == FUsdSchemaTranslator::ECollapsingType::Components; }
	virtual bool CanBeCollapsed( ECollapsingType CollapsingType ) const override { return false; }
};

#endif // #if USE_USD_SDK
