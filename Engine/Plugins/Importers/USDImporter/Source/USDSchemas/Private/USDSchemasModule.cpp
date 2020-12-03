// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDSchemasModule.h"

#include "USDSchemaTranslator.h"

#include "Modules/ModuleManager.h"

#if USE_USD_SDK
#include "USDGeomCameraTranslator.h"
#include "USDGeomMeshTranslator.h"
#include "USDGeomPointInstancerTranslator.h"
#include "USDGeomXformableTranslator.h"
#include "USDLuxLightTranslator.h"
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
#if WITH_EDITOR
		// Creating skeletal meshes technically works in Standalone mode, but by checking for this we artificially block it
		// to not confuse users as to why it doesn't work at runtime. Not registering the actual translators lets the inner meshes get parsed as static meshes, at least.
		if ( GIsEditor )
		{
			UsdSkelRootTranslatorHandle = GetTranslatorRegistry().Register< FUsdSkelRootTranslator >( TEXT("UsdSkelRoot") );
		}
#endif // WITH_EDITOR
		UsdGeomXformableTranslatorHandle = GetTranslatorRegistry().Register< FUsdGeomXformableTranslator >( TEXT("UsdGeomXformable") );
		UsdShadeMaterialTranslatorHandle = GetTranslatorRegistry().Register< FUsdShadeMaterialTranslator >( TEXT("UsdShadeMaterial") );
		UsdLuxLightTranslatorHandle = GetTranslatorRegistry().Register< FUsdLuxLightTranslator >( TEXT("UsdLuxLight") );
#endif // #if USE_USD_SDK
	}

	virtual void ShutdownModule() override
	{
#if USE_USD_SDK
		GetTranslatorRegistry().Unregister( UsdGeomCameraTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdGeomMeshTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdGeomPointInstancerTranslatorHandle );
#if WITH_EDITOR
		if ( GIsEditor )
		{
			GetTranslatorRegistry().Unregister( UsdSkelRootTranslatorHandle );
		}
#endif // WITH_EDITOR
		GetTranslatorRegistry().Unregister( UsdGeomXformableTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdShadeMaterialTranslatorHandle );
		GetTranslatorRegistry().Unregister( UsdLuxLightTranslatorHandle );
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
	FRegisteredSchemaTranslatorHandle UsdLuxLightTranslatorHandle;
};

IMPLEMENT_MODULE_USD( FUsdSchemasModule, USDSchemas );
