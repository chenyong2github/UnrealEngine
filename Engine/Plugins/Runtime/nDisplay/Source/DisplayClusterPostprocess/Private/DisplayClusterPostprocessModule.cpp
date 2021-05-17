// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPostprocessModule.h"

#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "PostProcess/DisplayClusterPostprocessOutputRemap.h"
#if PLATFORM_WINDOWS
#include "PostProcess/Windows/DisplayClusterPostprocessTextureShare.h"
#include "PostProcess/Windows/DisplayClusterPostprocessDX12CrossGPU.h"
#endif

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"


FDisplayClusterPostprocessModule::FDisplayClusterPostprocessModule()
{
	TSharedPtr<IDisplayClusterPostProcess, ESPMode::ThreadSafe> Postprocess;

	// Output Remap
	Postprocess = MakeShared<FDisplayClusterPostprocessOutputRemap, ESPMode::ThreadSafe>();
	PostprocessAssets.Emplace(DisplayClusterPostprocessStrings::postprocess::OutputRemap, Postprocess);

#if PLATFORM_WINDOWS
	// Texture Share
	//Postprocess = MakeShared<FDisplayClusterPostprocessTextureShare, ESPMode::ThreadSafe>();
	//PostprocessAssets.Emplace(DisplayClusterPostprocessStrings::postprocess::TextureShare, Postprocess);

	// D3D12 Cross GPU
	//Postprocess = MakeShared<FDisplayClusterPostprocessD3D12CrossGPU, ESPMode::ThreadSafe>();
	//PostprocessAssets.Emplace(DisplayClusterPostprocessStrings::postprocess::D3D12CrossGPU, Postprocess);
#endif

	UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Postprocess module has been instantiated"));
}

FDisplayClusterPostprocessModule::~FDisplayClusterPostprocessModule()
{
	UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Postprocess module has been destroyed"));
}


//////////////////////////////////////////////////////////////////////////////////////////////
// IModuleInterface
//////////////////////////////////////////////////////////////////////////////////////////////
void FDisplayClusterPostprocessModule::StartupModule()
{
	UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Postprocess module startup"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = PostprocessAssets.CreateIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Registering <%s> projection policy factory..."), *it->Key);

			if (!RenderMgr->RegisterPostprocessOperation(it->Key, it->Value))
			{
				UE_LOG(LogDisplayClusterPostprocess, Warning, TEXT("Couldn't register <%s> projection policy factory"), *it->Key);
			}
		}
	}

	UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Postprocess module has started"));
}

void FDisplayClusterPostprocessModule::ShutdownModule()
{
	UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Postprocess module shutdown"));

	IDisplayClusterRenderManager* RenderMgr = IDisplayCluster::Get().GetRenderMgr();
	if (RenderMgr)
	{
		for (auto it = PostprocessAssets.CreateConstIterator(); it; ++it)
		{
			UE_LOG(LogDisplayClusterPostprocess, Log, TEXT("Un-registering <%s> projection factory..."), *it->Key);

			if (!RenderMgr->UnregisterPostprocessOperation(it->Key))
			{
				UE_LOG(LogDisplayClusterPostprocess, Warning, TEXT("An error occurred during un-registering the <%s> projection factory"), *it->Key);
			}
		}
	}
}

IMPLEMENT_MODULE(FDisplayClusterPostprocessModule, DisplayClusterPostprocess)
