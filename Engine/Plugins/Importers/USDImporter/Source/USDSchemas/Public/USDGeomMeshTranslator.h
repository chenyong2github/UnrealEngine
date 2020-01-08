// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomXformableTranslator.h"

#include "MeshDescription.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/mesh.h"
#include "USDIncludesEnd.h"

class UStaticMesh;

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomMesh;
PXR_NAMESPACE_CLOSE_SCOPE

class FGeomMeshCreateAssetsTaskChain : public FUsdSchemaTranslatorTaskChain
{
public:
	FGeomMeshCreateAssetsTaskChain( TSharedRef< FUsdSchemaTranslationContext > InContext, const TUsdStore< pxr::UsdGeomMesh > InGeomMesh )
		: Context( InContext )
		, GeomMesh( InGeomMesh )
	{
		SetupTasks();
	}

public:
	// Inputs
	TSharedRef< FUsdSchemaTranslationContext > Context;
	TUsdStore< pxr::UsdGeomMesh > GeomMesh;

	// Outputs
	FMeshDescription MeshDescription;
	UStaticMesh* StaticMesh = nullptr;

protected:
	void SetupTasks();
};

class USDSCHEMAS_API FUsdGeomMeshTranslator : public FUsdGeomXformableTranslator
{
public:
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	FUsdGeomMeshTranslator( const FUsdGeomMeshTranslator& Other ) = delete;
	FUsdGeomMeshTranslator& operator=( const FUsdGeomMeshTranslator& Other ) = delete;

	virtual void CreateAssets() override;
	virtual USceneComponent* CreateComponents() override;

};

#endif // #if USE_USD_SDK
