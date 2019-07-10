// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMPCDI.h"

class FMPCDIData;


class FMPCDIModule : public IMPCDI
{
public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IModuleInterface
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

public:
	//////////////////////////////////////////////////////////////////////////////////////////////
	// IMPCDI
	//////////////////////////////////////////////////////////////////////////////////////////////
	virtual bool Load(const FString& MPCDIFile) override;
	virtual bool IsLoaded(const FString& MPCDIFile) override;
	virtual bool GetRegionLocator(const FString& MPCDIFile, const FString& BufferName, const FString& RegionName, IMPCDI::FRegionLocator& OutRegionLocator) override;
	virtual bool ComputeFrustum(const IMPCDI::FRegionLocator& RegionLocator, float WorldScale, float ZNear, float ZFar, IMPCDI::FFrustum& InOutFrustum) override;
	virtual bool ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& TextureWarpData, IMPCDI::FShaderInputData& ShaderInputData) override;
	virtual TSharedPtr<FMPCDIData> GetMPCDIData(IMPCDI::FShaderInputData& ShaderInputData) override;

private:
	void ReleaseMPCDIData();

private:
	FCriticalSection DataGuard;
	TArray<TSharedPtr<FMPCDIData>> MPCDIData;
};
