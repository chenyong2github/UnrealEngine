// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterPostprocessModule.h"

#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "PostProcess/DisplayClusterPostprocessOutputRemap.h"

#include "IDisplayCluster.h"
#include "Render/IDisplayClusterRenderManager.h"


FDisplayClusterPostprocessModule::FDisplayClusterPostprocessModule()
{
	TSharedPtr<IDisplayClusterPostProcess> Postprocess;

	// Output Remap
	Postprocess = MakeShareable(new FDisplayClusterPostprocessOutputRemap);
	PostprocessAssets.Emplace(DisplayClusterStrings::postprocess::OutputRemap, Postprocess);


	
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
