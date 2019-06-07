// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once 

class UStaticMesh;
struct FUsdImportContext;
struct FUsdAssetPrimToImport;
struct FUsdGeomData;

class FUSDStaticMeshImporter
{
#if USE_USD_SDK
public:
	static UStaticMesh* ImportStaticMesh(FUsdImportContext& ImportContext, const FUsdAssetPrimToImport& PrimToImport);
#endif // #if USE_USD_SDK
};