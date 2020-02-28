// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomXformableTranslator.h"

#include "MeshDescription.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdGeom/mesh.h"
#include "USDIncludesEnd.h"

class UStaticMesh;
class FStaticMeshComponentRecreateRenderStateContext;

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomMesh;
PXR_NAMESPACE_CLOSE_SCOPE

class FBuildStaticMeshTaskChain : public FUsdSchemaTranslatorTaskChain
{
public:
	explicit FBuildStaticMeshTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdTyped >& InSchema, FMeshDescription&& InMeshDescription );

protected:
	// Inputs
	// When multiple meshes are collapsed together, this Schema might not be the same as the Context schema, which is the root schema
	TUsdStore< pxr::UsdTyped > Schema;
	TSharedRef< FUsdSchemaTranslationContext > Context;
	FMeshDescription MeshDescription;

	// Outputs
	UStaticMesh* StaticMesh = nullptr;

	// Required to prevent StaticMesh from being used for drawing while it is being rebuilt
	TSharedPtr<FStaticMeshComponentRecreateRenderStateContext> RecreateRenderStateContextPtr;

protected:
	FBuildStaticMeshTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdTyped >& InSchema )
		: Schema( InSchema )
		, Context( InContext )
	{
	}

	virtual void SetupTasks();
};

class FGeomMeshCreateAssetsTaskChain : public FBuildStaticMeshTaskChain
{
public:
	explicit FGeomMeshCreateAssetsTaskChain( const TSharedRef< FUsdSchemaTranslationContext >& InContext, const TUsdStore< pxr::UsdGeomMesh >& InGeomMesh )
		: FBuildStaticMeshTaskChain( InContext, TUsdStore< pxr::UsdTyped >( InGeomMesh.Get() ) )
	{
		SetupTasks();
	}

protected:
	virtual void SetupTasks() override;
};

class USDSCHEMAS_API FUsdGeomMeshTranslator : public FUsdGeomXformableTranslator
{
public:
	using Super = FUsdGeomXformableTranslator;

	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	FUsdGeomMeshTranslator( const FUsdGeomMeshTranslator& Other ) = delete;
	FUsdGeomMeshTranslator& operator=( const FUsdGeomMeshTranslator& Other ) = delete;

	virtual void CreateAssets() override;
	virtual void UpdateComponents( USceneComponent* SceneComponent ) override;

	virtual bool CanBeCollapsed( ECollapsingType CollapsingType ) const override;

};

#endif // #if USE_USD_SDK
