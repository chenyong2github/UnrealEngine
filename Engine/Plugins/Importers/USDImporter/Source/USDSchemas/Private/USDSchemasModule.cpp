// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSchemasModule.h"

#include "USDSchemaTranslator.h"

#if USE_USD_SDK
#include "USDGeomCameraTranslator.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomPointInstancerTranslator.h"
#include "USDGeomXformableTranslator.h"
#include "USDMemory.h"
#include "USDShadeMaterialTranslator.h"
#include "USDSkelRootTranslator.h"
#endif // #if USE_USD_SDK

class FUsdSchemasModule : public IUsdSchemasModule
{
public:
	virtual void StartupModule() override
	{
#if USE_USD_SDK
		// Register the default translators
		UsdGeomCameraTranslatorHandle = GetTranslatorRegistry().Register< FUsdGeomCameraTranslator >( TEXT("UsdGeomCamera") );
		UsdGeomMeshTranslatorHandle = GetTranslatorRegistry().Register< FUsdGeomMeshTranslator >( TEXT("UsdGeomMesh") );
		UsdGeomPointInstancerTranslatorHandle = GetTranslatorRegistry().Register< FUsdGeomPointInstancerTranslator >( TEXT("UsdGeomPointInstancer") );
		UsdSkelRootTranslatorHandle = GetTranslatorRegistry().Register< FUsdSkelRootTranslator >( TEXT("UsdSkelRoot") );
		UsdGeomXformableTranslatorHandle = GetTranslatorRegistry().Register< FUsdGeomXformableTranslator >( TEXT("UsdGeomXformable") );
		UsdShadeMaterialTranslatorHandle = GetTranslatorRegistry().Register< FUsdShadeMaterialTranslator >( TEXT("UsdShadeMaterial") );
#endif // #if USE_USD_SDK
	}

	virtual void ShutdownModule() override
	{
#if USE_USD_SDK
		GetTranslatorRegistry().Unregister( UsdGeomCameraTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdGeomMeshTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdGeomPointInstancerTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdSkelRootTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdGeomXformableTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdShadeMaterialTranslatorHandle );
#endif // #if USE_USD_SDK
	}

	virtual FUsdSchemaTranslatorRegistry& GetTranslatorRegistry() override
	{
		return UsdSchemaTranslatorRegistry;
	}

protected:
	FUsdSchemaTranslatorRegistry UsdSchemaTranslatorRegistry;

	FRegisteredSchemaTranslatorHandle UsdGeomCameraTranslatorHandle;
	FRegisteredSchemaTranslatorHandle UsdGeomMeshTranslatorHandle;
	FRegisteredSchemaTranslatorHandle UsdGeomPointInstancerTranslatorHandle;
	FRegisteredSchemaTranslatorHandle UsdSkelRootTranslatorHandle;
	FRegisteredSchemaTranslatorHandle UsdGeomXformableTranslatorHandle;
	FRegisteredSchemaTranslatorHandle UsdShadeMaterialTranslatorHandle;
};

IMPLEMENT_MODULE_USD( FUsdSchemasModule, USDSchemas );
