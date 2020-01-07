// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/MPCDIBlueprintAPIImpl.h"

#include "Engine/TextureRenderTarget2D.h"

#include "UObject/Package.h"

#include "IMPCDI.h"
#include "MPCDILog.h"
#include "MPCDIRegion.h"
#include "MPCDIData.h"
#include "MPCDIWarpTexture.h"

#include "MPCDIStrings.h"

bool UMPCDIAPIImpl::GetMPCDIMeshData(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, struct FMPCDIGeometryExportData& MeshData)
{	
	IMPCDI& MpcdiModule = IMPCDI::Get();

	if (!MpcdiModule.Load(MPCDIFile))
	{
		//! err
		return false;
	}

	IMPCDI::FRegionLocator RegionLocator;
	bool bResult = MpcdiModule.GetRegionLocator(MPCDIFile, BufferName, RegionName, RegionLocator);
	if (bResult)
	{
		IMPCDI::FShaderInputData ShaderInputData;
		ShaderInputData.RegionLocator = RegionLocator;
		TSharedPtr<FMPCDIData> MpcdiData = MpcdiModule.GetMPCDIData(ShaderInputData);

		MPCDI::FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator);
		MPCDI::FMPCDIWarpTexture* WarpTexture = &Region->WarpMap;
		WarpTexture->ExportMeshData(MeshData);
		return true;
	}
	return false;
}

bool UMPCDIAPIImpl::GetPFMMeshData(const FString& LocalPFMFile, FMPCDIGeometryExportData& MeshData, float PFMScale, bool bIsMPCDIAxis)
{
	FString DefID(DisplayClusterStrings::cfg::data::mpcdi::PFMFileDefaultID);
	
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

	MPCDI::FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator);
	MPCDI::FMPCDIWarpTexture* WarpTexture = &Region->WarpMap;
	WarpTexture->ExportMeshData(MeshData);
	return true;
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

		MPCDI::FMPCDIRegion* Region = MpcdiData.Get()->GetRegion(RegionLocator);
		MPCDI::FMPCDIWarpTexture* WarpTexture = &Region->WarpMap;

		WarpTexture->ImportMeshData(MeshData);
	}
}
