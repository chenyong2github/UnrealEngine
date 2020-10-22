// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"
#include "UObject/Package.h"

#include "IMPCDI.h"
#include "MPCDICommon.h"
#include "MPCDIData.h"
#include "MPCDILog.h"
#include "MPCDIRegion.h"
#include "MPCDIStrings.h"
#include "MPCDIWarpTexture.h"


bool UMPCDIAPIImpl::GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, struct FMPCDIGeometryExportData& MeshData)
{
	IMPCDI& MpcdiModule = IMPCDI::Get();
	return MpcdiModule.GetMPCDIMeshData(MPCDIFile, BufferName, RegionName, MeshData);
}

bool UMPCDIAPIImpl::GetPFMMeshData(const FString& LocalPFMFile, FMPCDIGeometryExportData& MeshData, float PFMScale, bool bIsMPCDIAxis)
{
	FString DefID(DisplayClusterMPCDIStrings::cfg::PFMFileDefaultID);
	
	IMPCDI::FRegionLocator RegionLocator;

	IMPCDI& MpcdiModule = IMPCDI::Get();

	if (MpcdiModule.CreateCustomRegion(DefID, DefID, LocalPFMFile, RegionLocator))
	{
		MpcdiModule.SetMPCDIProfileType(RegionLocator, IMPCDI::EMPCDIProfileType::mpcdi_A3D);		
	}

	//! todo: Force reload
	if (!MpcdiModule.LoadPFM(RegionLocator, LocalPFMFile, PFMScale, !bIsMPCDIAxis))
	{
		UE_LOG(LogMPCDI, Error, TEXT("Failed to load PFM from file: %s"), *LocalPFMFile);
		return false;
	}

	// Get region warp mesh data:
	IMPCDI::FShaderInputData ShaderInputData;
	ShaderInputData.RegionLocator = RegionLocator;
	TSharedPtr<FMPCDIData> MpcdiData = MpcdiModule.GetMPCDIData(ShaderInputData);

	FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator);
	return ExportMeshData(Region, MeshData);
}

void UMPCDIAPIImpl::ReloadChangedExternalFiles()
{
	IMPCDI::Get().ReloadAll();
}

void UMPCDIAPIImpl::SetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, const struct FMPCDIGeometryImportData& MeshData)
{
	IMPCDI& MpcdiModule = IMPCDI::Get();
	IMPCDI::FRegionLocator RegionLocator;
	bool bResult = MpcdiModule.GetRegionLocator(MPCDIFile, BufferName, RegionName, RegionLocator);

	if (bResult)
	{
		IMPCDI::FShaderInputData ShaderInputData;
		ShaderInputData.RegionLocator = RegionLocator;
		TSharedPtr<FMPCDIData> MpcdiData = MpcdiModule.GetMPCDIData(ShaderInputData);

		FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator);
		ImportMeshData(Region, MeshData);
	}
}
