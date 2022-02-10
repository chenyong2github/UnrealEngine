// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRenderTargetResourcesPool.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"
#include "RHICommandList.h"
#include "Engine/RendererSettings.h"
#include "RenderingThread.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////
namespace DisplayClusterRenderTargetResourcesPool
{
	static void ImplInitializeViewportResourcesRHI(const TArrayView<FDisplayClusterViewportResource*>& InResources)
	{
		TArray<FDisplayClusterViewportResource*> ResourcesForRenderThread(InResources);

		ENQUEUE_RENDER_COMMAND(DisplayCluster_InitializeViewportResourcesRHI)(
			[NewResources = std::move(ResourcesForRenderThread)](FRHICommandListImmediate& RHICmdList)
		{
			for (FDisplayClusterViewportResource* ResourceIt : NewResources)
			{
				ResourceIt->InitResource();
			}
		});
	}

	static void ImplReleaseViewportResourcesRHI(const TArrayView<FDisplayClusterViewportResource*>& InResources)
	{
		TArray<FDisplayClusterViewportResource*> ResourcesForRenderThread(InResources);

		ENQUEUE_RENDER_COMMAND(DisplayCluster_ReleaseViewportResourcesRHI)(
			[ReleasedResources = std::move(ResourcesForRenderThread)](FRHICommandListImmediate& RHICmdList)
		{
			for (FDisplayClusterViewportResource* ResourceIt : ReleasedResources)
			{
				ResourceIt->ReleaseResource();
				delete ResourceIt;
			}
		});
	}

	static FDisplayClusterViewportResourceSettings* ImplCreateViewportResourceSettings(const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, FViewport* InViewport)
	{
		FDisplayClusterViewportResourceSettings Result;

		if (InViewport != nullptr)
		{
			FRHITexture2D* ViewportTexture = InViewport->GetRenderTargetTexture();
			if (ViewportTexture != nullptr)
			{
				EPixelFormat Format = ViewportTexture->GetFormat();
				bool bShouldUseSRGB = EnumHasAnyFlags(ViewportTexture->GetFlags(), TexCreate_SRGB);
				float Gamma = InViewport->GetDisplayGamma();

				return new FDisplayClusterViewportResourceSettings(InRenderFrameSettings.ClusterNodeId, Format, bShouldUseSRGB, Gamma);
			}
		}

		// Get default settings for preview rendering:
		static TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		EPixelFormat Format = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));

		// default gamma for preview in editor
		float Gamma = 2.2f;
		switch (InRenderFrameSettings.RenderMode)
		{
		case EDisplayClusterRenderFrameMode::PreviewInScene:
			Gamma = 1.f;
			break;
		default:
			break;
		}

		// By default sRGB disabled (Preview rendering)
		bool bShouldUseSRGB = false;

		return new FDisplayClusterViewportResourceSettings(InRenderFrameSettings.ClusterNodeId, Format, bShouldUseSRGB, Gamma);
	}

	static bool IsTextureSizeValidImpl(const FIntPoint& InSize)
	{
		static const int32 MaxTextureSize = 1 << (GMaxTextureMipCount - 1);

		// just check the texture size is valid
		if (InSize.X <= 0 || InSize.Y <= 0 || InSize.X > MaxTextureSize || InSize.Y > MaxTextureSize)
		{
			return false;
		}


		//@todo - maybe check free size of video memory before texture allocation

		return true;
	}
};

using namespace DisplayClusterRenderTargetResourcesPool;

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FDisplayClusterRenderTargetResourcesPool
////////////////////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterRenderTargetResourcesPool::FDisplayClusterRenderTargetResourcesPool()
{
}

FDisplayClusterRenderTargetResourcesPool::~FDisplayClusterRenderTargetResourcesPool()
{
	// Release all resources
	ImplReleaseResources<FDisplayClusterViewportRenderTargetResource>(RenderTargetResources);
	ImplReleaseResources<FDisplayClusterViewportTextureResource>(TextureResources);
}

template <typename TViewportResourceType>
void FDisplayClusterRenderTargetResourcesPool::ImplBeginReallocateResources(TArray<TViewportResourceType*>& InOutViewportResources)
{
	check(ResourceSettings != nullptr);

	// Mark cluster node resources as unused
	for (TViewportResourceType* ResourceIt : InOutViewportResources)
	{
		if (ResourceIt != nullptr && ResourceIt->GetResourceSettingsConstRef().IsClusterNodeNameEqual(*ResourceSettings))
		{
			ResourceIt->RaiseViewportResourceState(EDisplayClusterViewportResourceState::Unused);
		}
	}
}

template <typename TViewportResourceType>
void FDisplayClusterRenderTargetResourcesPool::ImplFinishReallocateResources(TArray<TViewportResourceType*>& InOutViewportResources)
{
	// Collect new and unused resources 
	TArray<TViewportResourceType*> NewResources;
	TArray<TViewportResourceType*> UnusedResources;

	for (TViewportResourceType* ResourceIt : InOutViewportResources)
	{
		if (ResourceIt != nullptr)
		{
			check(ResourceIt->GetViewportResourceState(EDisplayClusterViewportResourceState::Deleted) == false);

			if (ResourceIt->GetViewportResourceState(EDisplayClusterViewportResourceState::Initialized) == false)
			{
				// Collect new resources
				NewResources.Add(ResourceIt);
				ResourceIt->RaiseViewportResourceState(EDisplayClusterViewportResourceState::Initialized);
			}
			else if (ResourceIt->GetViewportResourceState(EDisplayClusterViewportResourceState::Unused) == true)
			{
				// Collect unused resources
				UnusedResources.Add(ResourceIt);
				ResourceIt->RaiseViewportResourceState(EDisplayClusterViewportResourceState::Deleted);
			}
		}
	}

	// Init RHI for new resources
	if (NewResources.Num() > 0)
	{
		// Send to rendering thread
		ImplInitializeViewportResourcesRHI(TArrayView<FDisplayClusterViewportResource*>((FDisplayClusterViewportResource**)(NewResources.GetData()), NewResources.Num()));
	}

	// Remove unused resources
	if (UnusedResources.Num() > 0)
	{
		// Remove GameThread resource references
		for (TViewportResourceType* ResourceIt : UnusedResources)
		{
			int ResourceIndex = INDEX_NONE;
			while (InOutViewportResources.Find(ResourceIt, ResourceIndex))
			{
				InOutViewportResources.RemoveAt(ResourceIndex);
			}
		}

		// Send to released resources to rendering thread
		ImplReleaseViewportResourcesRHI(TArrayView<FDisplayClusterViewportResource*>((FDisplayClusterViewportResource**)(UnusedResources.GetData()), UnusedResources.Num()));
	}
}

template <typename TViewportResourceType>
void FDisplayClusterRenderTargetResourcesPool::ImplReleaseResources(TArray<TViewportResourceType*>& InOutViewportResources)
{
	// Send all resources to rendering thread
	ImplReleaseViewportResourcesRHI(TArray<FDisplayClusterViewportResource*>((FDisplayClusterViewportResource**)(InOutViewportResources.GetData()), InOutViewportResources.Num()));

	// Reset refs on game thread too
	InOutViewportResources.Empty();
}

template <typename TViewportResourceType>
TViewportResourceType* FDisplayClusterRenderTargetResourcesPool::ImplAllocateResource(TArray<TViewportResourceType*>& InOutViewportResources, const FDisplayClusterViewportResourceSettings& InSettings)
{
	if (!IsTextureSizeValidImpl(InSettings.Size))
	{
		return nullptr;
	}

	// Unused resources marked for current cluster node
	auto ExistAndUnusedResourceIndex = InOutViewportResources.IndexOfByPredicate([InSettings](const TViewportResourceType* ResourceIt)
	{
		if (ResourceIt != nullptr && ResourceIt->GetViewportResourceState(EDisplayClusterViewportResourceState::Unused) == true)
		{
			return ResourceIt->GetResourceSettingsConstRef().IsResourceSettingsEqual(InSettings);
		}

		return false;
	});

	if (ExistAndUnusedResourceIndex != INDEX_NONE)
	{
		// Use exist resource again
		TViewportResourceType* ExistResource = InOutViewportResources[ExistAndUnusedResourceIndex];

		// Clear unused state for re-used resource
		ExistResource->ClearViewportResourceState(EDisplayClusterViewportResourceState::Unused);

		return ExistResource;
	}

	// Create new resource:
	TViewportResourceType* NewResource = new TViewportResourceType(InSettings);
	InOutViewportResources.Add(NewResource);

	return NewResource;
}

bool FDisplayClusterRenderTargetResourcesPool::BeginReallocateResources(const FDisplayClusterRenderFrameSettings& InRenderFrameSettings, FViewport* InViewport)
{
	check(ResourceSettings == nullptr);

	// Initialize settings for new render frame
	ResourceSettings = ImplCreateViewportResourceSettings(InRenderFrameSettings, InViewport);

	if(ResourceSettings != nullptr)
	{
		// Begin reallocate resources for current cluster node
		ImplBeginReallocateResources<FDisplayClusterViewportRenderTargetResource>(RenderTargetResources);
		ImplBeginReallocateResources<FDisplayClusterViewportTextureResource>(TextureResources);

		return true;
	}

	return false;
}

void FDisplayClusterRenderTargetResourcesPool::FinishReallocateResources()
{
	if (ResourceSettings != nullptr)
	{
		// Finish reallocate resources for current cluster node
		ImplFinishReallocateResources<FDisplayClusterViewportRenderTargetResource>(RenderTargetResources);
		ImplFinishReallocateResources<FDisplayClusterViewportTextureResource>(TextureResources);

		delete ResourceSettings;
		ResourceSettings = nullptr;
	}
}

FDisplayClusterViewportRenderTargetResource* FDisplayClusterRenderTargetResourcesPool::AllocateRenderTargetResource(const FIntPoint& InSize, enum EPixelFormat CustomPixelFormat)
{
	check(ResourceSettings != nullptr);

	return ImplAllocateResource<FDisplayClusterViewportRenderTargetResource>(RenderTargetResources, FDisplayClusterViewportResourceSettings(*ResourceSettings, InSize, CustomPixelFormat));
}

FDisplayClusterViewportTextureResource* FDisplayClusterRenderTargetResourcesPool::AllocateTextureResource(const FIntPoint& InSize, bool bIsRenderTargetable, enum EPixelFormat CustomPixelFormat, int32 InNumMips)
{
	check(ResourceSettings != nullptr);

	return ImplAllocateResource<FDisplayClusterViewportTextureResource>(TextureResources, FDisplayClusterViewportResourceSettings(*ResourceSettings, InSize, CustomPixelFormat, bIsRenderTargetable, InNumMips));
}
