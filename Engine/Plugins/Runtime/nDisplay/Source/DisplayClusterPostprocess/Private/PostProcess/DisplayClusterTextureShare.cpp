// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterTextureShare.h"

#include "Engine/RendererSettings.h"
#include "Misc/Paths.h"

#include "DisplayClusterPostprocessHelpers.h"
#include "DisplayClusterPostprocessLog.h"
#include "DisplayClusterPostprocessStrings.h"

#include "Render/IDisplayClusterRenderManager.h"
#include "Render/Projection/IDisplayClusterProjectionPolicy.h"
#include "Render/Device/DisplayClusterRenderViewport.h"

#include "ITextureShare.h"
#include "ITextureShareItem.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// IDisplayClusterPostProcess
//////////////////////////////////////////////////////////////////////////////////////////////
ITextureShare& TextureShareAPI()
{
	static ITextureShare& SingletoneApi = ITextureShare::Get();
	return SingletoneApi;
}

FString GetDisplayClusterViewportShareName(const FString& ShareName)
{
	//@todo: Make unique share name?
	//static constexpr auto DefaultShareName     = TEXT("nDisplay");
	//return FString::Printf(TEXT("%s_%s"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::DefaultShareName, *ShareName);
	return ShareName;
}

bool FDisplayClusterTextureShare::CreateResource(const FString& ShareName, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	//@todo: add custom sync setup
	FTextureShareSyncPolicy SyncPolicy;

	//Create internal share (DX12 MGPU)
	if (!TextureShareAPI().CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Server))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed create viewport share '%s'"), *ShareName);
		return false;
	}

	FScopeLock lock(&DataGuard);
	ShareResourceNames.AddUnique(ShareName);
	return true;
}

bool FDisplayClusterTextureShare::OpenResource(const FString& ShareName) const
{
	//@todo: add custom sync setup
	FTextureShareSyncPolicy SyncPolicy;
	SyncPolicy.TextureSync = ETextureShareSyncSurface::SyncPairingRead; // Viewport client process require external texture
	
	if (!TextureShareAPI().CreateShare(ShareName, SyncPolicy, ETextureShareProcess::Client))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed open share '%s'"), *ShareName);
		return false;
	}

	TSharedPtr<ITextureShareItem> ShareItem;
	if (!TextureShareAPI().GetShare(ShareName, ShareItem) || !ShareItem->RegisterTexture(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::BackbufferTextureId, FIntPoint(0, 0), ETextureShareFormat::Undefined, 0, ETextureShareSurfaceOp::Read))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Failed open share '%s' texture"), *ShareName);
		TextureShareAPI().ReleaseShare(ShareName);
		return false;
	}
	
	FScopeLock lock(&DataGuard);
	ShareResourceNames.AddUnique(ShareName);
	return true;
}

bool FDisplayClusterTextureShare::BeginSession() const
{
	FScopeLock lock(&DataGuard);

	for (auto& It : ShareResourceNames)
	{
		TSharedPtr<ITextureShareItem> ShareItem;
		if (TextureShareAPI().GetShare(It, ShareItem))
		{
			ShareItem->BeginSession();
		}
	}
	
	return true;
}

bool FDisplayClusterTextureShare::EndSession() const
{
	FScopeLock lock(&DataGuard);

	for (auto& It : ShareResourceNames)
	{
		TextureShareAPI().ReleaseShare(It);
	}

	ShareResourceNames.Empty();
	return true;
}

bool FDisplayClusterTextureShare::SendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ResourceID, ShareItem))
	{
		if (ShareItem->BeginFrame_RenderThread())
		{
			TextureShareAPI().SendTexture_RenderThread(RHICmdList, ShareItem, DisplayClusterStrings::cfg::data::postprocess::texture_sharing::BackbufferTextureId, SrcResource, &SrcTextureRect);
			ShareItem->EndFrame_RenderThread();
		}
	}

	return false;
}

bool FDisplayClusterTextureShare::ReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	TSharedPtr<ITextureShareItem> ShareItem;
	if (TextureShareAPI().GetShare(ResourceID, ShareItem))
	{
		if (ShareItem->BeginFrame_RenderThread())
		{
			TextureShareAPI().ReceiveTexture_RenderThread(RHICmdList, ShareItem, DisplayClusterStrings::cfg::data::postprocess::texture_sharing::BackbufferTextureId, DstResource, &DstTextureRect);
			ShareItem->EndFrame_RenderThread();
		}
	}

	return false;
}

bool FDisplayClusterTextureShare::ImplCreateResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FString ShareName = GetDisplayClusterViewportShareName(ResourceViewport.GetId());
	bool bResult = CreateResource_RenderThread(RHICmdList, ShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			CreateResource_RenderThread(RHICmdList, DupShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);
		}
	}

	return bResult;
}

bool FDisplayClusterTextureShare::ImplCreateResource(const FDisplayClusterRenderViewport& ResourceViewport, int ResourceViewportIndex, FRHITexture2D* ResourceTexture) const
{
	FString ShareName = GetDisplayClusterViewportShareName(ResourceViewport.GetId());
	bool bResult = CreateResource(ShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			CreateResource(DupShareName, ResourceViewport, ResourceViewportIndex, ResourceTexture);
		}
	}

	return bResult;
}

bool FDisplayClusterTextureShare::ImplOpenResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ShareName) const
{
	bool bResult = OpenResource_RenderThread(RHICmdList, ShareName);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			OpenResource_RenderThread(RHICmdList, DupShareName);
		}
	}

	return bResult;
}
bool FDisplayClusterTextureShare::ImplOpenResource(const FString& ShareName) const
{
	bool bResult = OpenResource(ShareName);

	//Test: open duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ShareName, i);
			OpenResource(DupShareName);
		}
	}

	return bResult;
}
bool FDisplayClusterTextureShare::ImplSendResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* SrcResource, const FIntRect& SrcTextureRect) const
{
	bool bResult = SendResource_RenderThread(RHICmdList, ResourceID, SrcResource, SrcTextureRect);

	//Test: send duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ResourceID, i);
			SendResource_RenderThread(RHICmdList, DupShareName, SrcResource, SrcTextureRect);
		}
	}

	return bResult;
}

bool FDisplayClusterTextureShare::ImplReceiveResource_RenderThread(FRHICommandListImmediate& RHICmdList, const FString& ResourceID, FRHITexture2D* DstResource, const FIntRect& DstTextureRect) const
{
	bool bResult = ReceiveResource_RenderThread(RHICmdList, ResourceID, DstResource, DstTextureRect);

	//Test: receive duplicates
	if (TestDuplicateTextures > 0)
	{
		for (int i = 0; i < TestDuplicateTextures; ++i)
		{
			FString DupShareName = FString::Printf(TEXT("%s_%u"), *ResourceID, i);
			ReceiveResource_RenderThread(RHICmdList, DupShareName, DstResource, DstTextureRect);
		}
	}

	return bResult;
}

void FDisplayClusterTextureShare::PerformUpdateViewport(FViewport* MainViewport, const TArray<FDisplayClusterRenderViewport>& RenderViewports)
{
	static bool bResourceInitialized = false;
	if (!bResourceInitialized)
	{
		bResourceInitialized = true;
		InitializeResources(MainViewport, RenderViewports);
	}
}

bool FDisplayClusterTextureShare::IsPostProcessRenderTargetBeforeWarpBlendRequired()
{
	return bIsEnabled;
}

void FDisplayClusterTextureShare::PerformPostProcessRenderTargetBeforeWarpBlend_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const
{
	static bool bResourceInitialized = false;
	if (!bResourceInitialized)
	{
		bResourceInitialized = true;
		InitializeResources_RenderThread(RHICmdList, InOutTexture, RenderViewports);
	}

	SendViewports_RenderThread(RHICmdList, InOutTexture, RenderViewports);
	ReceiveViewports_RenderThread(RHICmdList, InOutTexture);
}

void FDisplayClusterTextureShare::InitializeResources_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const
{
	// Create master share for all render viewports
	for (int CurrentViewportIndex = 0; CurrentViewportIndex < RenderViewports.Num(); CurrentViewportIndex++)
	{
		const FDisplayClusterRenderViewport& CurrentViewport = RenderViewports[CurrentViewportIndex];
		if (ShareViewportsMap.Contains(CurrentViewport.GetId()))
		{
			ImplCreateResource_RenderThread(RHICmdList, CurrentViewport, CurrentViewportIndex, InOutTexture);
		}
	}

	// Open slave share for all render viewports
	for (auto& It : DestinationViewportsMap)
	{
		ImplOpenResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value));
	}

	BeginSession_RenderThread(RHICmdList);
}


void FDisplayClusterTextureShare::InitializeResources(FViewport* MainViewport, const TArray<FDisplayClusterRenderViewport>& RenderViewports)
{
	const FTexture2DRHIRef& Backbuffer = MainViewport->GetRenderTargetTexture();

	// Create master share for all render viewports
	for (int CurrentViewportIndex = 0; CurrentViewportIndex < RenderViewports.Num(); CurrentViewportIndex++)
	{
		const FDisplayClusterRenderViewport& CurrentViewport = RenderViewports[CurrentViewportIndex];
		if (ShareViewportsMap.Contains(CurrentViewport.GetId()))
		{
			ImplCreateResource(CurrentViewport, CurrentViewportIndex, Backbuffer);
		}
	}

	// Open slave share for all render viewports
	for (auto& It : DestinationViewportsMap)
	{
		ImplOpenResource(GetDisplayClusterViewportShareName(It.Value));
	}

	BeginSession();
}

void FDisplayClusterTextureShare::SendViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture, const TArray<FDisplayClusterRenderViewport>& RenderViewports) const
{
	//Send all render viewports
	for (auto& It : RenderViewports)
	{
		if (ShareViewportsMap.Contains(It.GetId()))
		{
			ImplSendResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.GetId()), InOutTexture, It.GetArea());

			//Test: Send duplicates
			for (int i = 0; i < TestRepeatCopy; ++i)
			{
				ImplSendResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.GetId()), InOutTexture, It.GetArea());
			}
		}
	}
}

void FDisplayClusterTextureShare::ReceiveViewports_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture2D* InOutTexture) const
{
	//@todo: replace static links
	static IDisplayClusterConfigManager* ConfigManager = IDisplayCluster::Get().GetConfigMgr();

	// Receive to all defined in config viewports namespace
	for (auto& It : DestinationViewportsMap)
	{
		FDisplayClusterConfigViewport SourceViewport;
		if (ConfigManager->GetViewport(It.Key, SourceViewport))
		{
			ImplReceiveResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value), InOutTexture, FIntRect(SourceViewport.Loc, SourceViewport.Loc + SourceViewport.Size));

			//Test: Send duplicates
			for (int i = 0; i < TestRepeatCopy; ++i)
			{
				ImplReceiveResource_RenderThread(RHICmdList, GetDisplayClusterViewportShareName(It.Value), InOutTexture, FIntRect(SourceViewport.Loc, SourceViewport.Loc + SourceViewport.Size));
			}
		}
	}
}

void FDisplayClusterTextureShare::Release()
{
	if (bIsEnabled)
	{
		bIsEnabled = false;
		//@todo add release
	}
}

void FDisplayClusterTextureShare::InitializePostProcess(const FString& CfgLine)
{
	TArray<FString> CfgShareViewports;
	if (DisplayClusterHelpers::str::ExtractArray(CfgLine, FString(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::ShareViewports), FString(DisplayClusterStrings::common::ArrayValSeparator), CfgShareViewports))
	{
		FString DbgLog;
		for (auto& It : CfgShareViewports)
		{
			ShareViewportsMap.Add(It.ToLower(), true);
			bIsEnabled = true;
			DbgLog.Append(It);
			DbgLog.Append(DisplayClusterStrings::common::ArrayValSeparator);
		}
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::ShareViewports, *DbgLog);
	}

	TArray<FString> CfgSourceTextures;
	if (DisplayClusterHelpers::str::ExtractArray(CfgLine, FString(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::SourceShare), FString(DisplayClusterStrings::common::ArrayValSeparator), CfgSourceTextures))
	{
		FString DbgLog;
		for (auto& It : CfgSourceTextures)
		{
			DbgLog.Append(It);
			DbgLog.Append(DisplayClusterStrings::common::ArrayValSeparator);
		}
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::SourceShare, *DbgLog);
	}

	TArray<FString> CfgDestinationViewports;
	if (DisplayClusterHelpers::str::ExtractArray(CfgLine, FString(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::DestinationViewports), FString(DisplayClusterStrings::common::ArrayValSeparator), CfgDestinationViewports))
	{
		FString DbgLog;
		for (auto& It : CfgDestinationViewports)
		{
			DbgLog.Append(It);
			DbgLog.Append(DisplayClusterStrings::common::ArrayValSeparator);
		}
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'='%s'"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::DestinationViewports, *DbgLog);
	}


	if (CfgSourceTextures.Num() != CfgDestinationViewports.Num())
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Error, TEXT("Argument count in '%s' and '%s' must be equal"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::SourceShare, DisplayClusterStrings::cfg::data::postprocess::texture_sharing::DestinationViewports);
		return;
	}

	// Create viewports to texture map
	for (int i = 0;i < CfgSourceTextures.Num();i++)
	{
		DestinationViewportsMap.Add(CfgDestinationViewports[i].ToLower(), CfgSourceTextures[i].ToLower());
		bIsEnabled = true;
	}

	// Get test params
	if (DisplayClusterHelpers::str::ExtractValue(CfgLine, FString(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::debug::DuplicateTexture), TestDuplicateTextures))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'=%u"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::debug::DuplicateTexture, TestDuplicateTextures);
	}
	if (DisplayClusterHelpers::str::ExtractValue(CfgLine, FString(DisplayClusterStrings::cfg::data::postprocess::texture_sharing::debug::RepeatCopy), TestRepeatCopy))
	{
		UE_LOG(LogDisplayClusterPostprocessTextureShare, Log, TEXT("Found Argument '%s'=%u"), DisplayClusterStrings::cfg::data::postprocess::texture_sharing::debug::RepeatCopy, TestRepeatCopy);
	}

}
