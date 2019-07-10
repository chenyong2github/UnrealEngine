// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"

#include "UObject/Package.h"

#include "IMPCDI.h"
#include "MPCDIRegion.h"
#include "MPCDIData.h"
#include "MPCDIWarpTexture.h"


void UMPCDIAPIImpl::GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, struct FMPCDIGeometryExportData& MeshData)
{	
	IMPCDI& mpcdimodule = IMPCDI::Get();
	IMPCDI::FRegionLocator regionLocator;
	bool res = mpcdimodule.GetRegionLocator(MPCDIFile, BufferName, RegionName, regionLocator);

	if (res)
	{
		IMPCDI::FShaderInputData ShaderInputData;
		ShaderInputData.RegionLocator = regionLocator;
		TSharedPtr<FMPCDIData> mpcdiData = mpcdimodule.GetMPCDIData(ShaderInputData);

		MPCDI::FMPCDIRegion* region = mpcdiData.Get()->GetRegion(regionLocator);
		MPCDI::FMPCDIWarpTexture* warpTexture = &region->WarpMap;
		
		warpTexture->ExportMeshData(MeshData);
	}
}
