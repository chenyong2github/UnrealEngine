// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"

FDynamicRHI* PlatformCreateDynamicRHI()
{
	FDynamicRHI* DynamicRHI = NULL;
	IDynamicRHIModule* DynamicRHIModule = NULL;

#if WITH_D3D12_RHI
	bool bConfigRequestsD3D12 = false;
	GConfig->GetBool(TEXT("/Script/HoloLensPlatformEditor.HoloLensTargetSettings"), TEXT("bUseD3D12RHI"), bConfigRequestsD3D12, GEngineIni);
	const bool bForceD3D12 = FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12"));

	if (bForceD3D12 || bConfigRequestsD3D12)
	{
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("D3D12RHI"));
	}
	else
#endif
	{
		// Load the dynamic RHI module.
		auto& FMan = FModuleManager::Get();
		DynamicRHIModule = (IDynamicRHIModule*)FMan.GetModule(TEXT("WindowsMixedRealityRHI"));
		if (DynamicRHIModule == nullptr)
		{
			DynamicRHIModule = (IDynamicRHIModule*)FMan.LoadModule(TEXT("D3D11RHI"));
		}
	}

	// Create the dynamic RHI.
	if (!DynamicRHIModule->IsSupported())
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("HoloLens", "FailedToCreateHoloLens_RHI", "HoloLensRHI failure?"));
		FPlatformMisc::RequestExit(1);
		DynamicRHIModule = NULL;
	}
	else
	{
		DynamicRHI = DynamicRHIModule->CreateRHI();
	}

#if PLATFORM_HOLOLENS
	GMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;
	GMaxRHIShaderPlatform = SP_PCD3D_ES3_1;
#endif

	return DynamicRHI;
}
