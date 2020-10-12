// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDICommon.h"

#include "MPCDIRegion.h"
#include "MPCDIWarp.h"
#include "MPCDIWarpTexture.h"


bool ExportMeshData(FMPCDIRegion* Region, struct FMPCDIGeometryExportData& MeshData)
{
	if (Region->WarpData)
	{
		switch (Region->WarpData->GetWarpGeometryType())
		{
		case EWarpGeometryType::PFM_Texture:
		{
			FMPCDIWarpTexture* WarpMap = static_cast<FMPCDIWarpTexture*>(Region->WarpData);
			WarpMap->ExportMeshData(MeshData);
			break;
		}
		case EWarpGeometryType::UE_StaticMesh:
		{
			//! Not Implemented
			return false;
			break;
		}
		}
	}

	return true;
}

bool ImportMeshData(FMPCDIRegion* Region, const struct FMPCDIGeometryImportData& MeshData)
{
	if (Region->WarpData)
	{
		switch (Region->WarpData->GetWarpGeometryType())
		{
		case EWarpGeometryType::PFM_Texture:
		{
			FMPCDIWarpTexture* WarpMap = static_cast<FMPCDIWarpTexture*>(Region->WarpData);
			WarpMap->ImportMeshData(MeshData);
			break;
		}

		case EWarpGeometryType::UE_StaticMesh:
		{
			//! Not Implemented
			return false;
		}
		}
	}

	return true;
}
