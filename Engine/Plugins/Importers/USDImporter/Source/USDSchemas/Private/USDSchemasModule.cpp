// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDSchemasModule.h"

#if USE_USD_SDK
#include "USDGeomCameraTranslator.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomPointInstancerTranslator.h"
#include "USDGeomXFormableTranslator.h"
#include "USDMemory.h"
#include "USDSchemaTranslator.h"
#include "USDSkelRootTranslator.h"
#endif // #if USE_USD_SDK

class FUsdSchemasModule : public IUsdSchemasModule
{
public:
	virtual void StartupModule() override
	{
#if USE_USD_SDK
		// Register the default translators
		FUsdGeomCameraTranslator::RegisterTranslator();
		FUsdGeomMeshTranslator::RegisterTranslator();
		FUsdGeomPointInstancerTranslator::RegisterTranslator();
		FUsdSkelRootTranslator::RegisterTranslator();
		FUsdGeomXformableTranslator::RegisterTranslator();
#endif // #if USE_USD_SDK
	}

	virtual FUsdSchemaTranslatorRegistry& GetTranslatorRegistry() override
	{
		return UsdSchemaTranslatorRegistry;
	}

protected:
	FUsdSchemaTranslatorRegistry UsdSchemaTranslatorRegistry;
};

IMPLEMENT_MODULE_USD( FUsdSchemasModule, USDSchemas );
