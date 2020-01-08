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
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;
	
	virtual USceneComponent* CreateComponents() override;
	virtual bool CollapsedHierarchy() const override { return true; }
};

#endif // #if USE_USD_SDK
