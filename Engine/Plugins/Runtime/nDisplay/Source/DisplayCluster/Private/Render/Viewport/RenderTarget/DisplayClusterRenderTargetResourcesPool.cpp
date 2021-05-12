// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterRenderTargetResourcesPool.h"

#include "DisplayClusterRenderTargetResource.h"
#include "Engine/RendererSettings.h"
#include "RenderingThread.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FDisplayClusterViewportResourceSettings
////////////////////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterViewportResourceSettings::FDisplayClusterViewportResourceSettings(class FViewport* InViewport)
{
	if (InViewport)
	{
		FRHITexture2D* ViewportTexture = InViewport->GetRenderTargetTexture();
		if (ViewportTexture)
		{
			Format = ViewportTexture->GetFormat();
			bShouldUseSRGB = (ViewportTexture->GetFlags() & TexCreate_SRGB) != 0;
			DisplayGamma = InViewport->GetDisplayGamma();
		}
	}
	else
	{
		// Get settings for preview rendering:
		static const TConsoleVariableData<int32>* CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		Format = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));

		// Use default values
		//@todo: Get gamma and SRGB from editor
		bShouldUseSRGB = false;
		DisplayGamma = 2.2f;
	}
}

FDisplayClusterViewportResourceSettings::FDisplayClusterViewportResourceSettings(class FRHITexture2D* InTexture)
{
	if (InTexture)
	{
		Size = InTexture->GetSizeXY();
		Format = InTexture->GetFormat();
		bShouldUseSRGB = (InTexture->GetFlags() & TexCreate_SRGB) != 0;

		bIsRenderTargetable = (InTexture->GetFlags() & TexCreate_RenderTargetable) != 0;
		NumMips = InTexture->GetNumMips();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FDisplayClusterRenderTargetResourcesPool
////////////////////////////////////////////////////////////////////////////////////////////////////////////
FDisplayClusterRenderTargetResourcesPool::FDisplayClusterRenderTargetResourcesPool()
{
}

FDisplayClusterRenderTargetResourcesPool::~FDisplayClusterRenderTargetResourcesPool()
{
	ReleaseRenderTargetResources(RenderTargetResources);
	ReleaseRenderTargetResources(UnusedRenderTargetResources);
	ReleaseTextureResources(TextureResources);
	ReleaseTextureResources(UnusedTextureResources);
}

bool FDisplayClusterRenderTargetResourcesPool::IsTextureSizeValid(const FIntPoint& InSize) const
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

// --------------------------------------------------------------------------------------------------------------------------------------

bool FDisplayClusterRenderTargetResourcesPool::BeginReallocateRenderTargetResources(class FViewport* InViewport)
{
	if (pRenderTargetResourceSettings == nullptr)
	{
		pRenderTargetResourceSettings = new FDisplayClusterViewportResourceSettings(InViewport);

		UnusedRenderTargetResources = RenderTargetResources;
		RenderTargetResources.Empty();
		return true;
	}

	return false;
}

FDisplayClusterRenderTargetResource* FDisplayClusterRenderTargetResourcesPool::AllocateRenderTargetResource(const FIntPoint& InSize, enum EPixelFormat CustomPixelFormat)
{
	if (pRenderTargetResourceSettings == nullptr || !IsTextureSizeValid(InSize))
	{
		return nullptr;
	}

	FDisplayClusterViewportResourceSettings RequiredResourceSettings = *pRenderTargetResourceSettings;
	RequiredResourceSettings.Size = InSize;

	if (CustomPixelFormat != PF_Unknown)
	{
		RequiredResourceSettings.Format = CustomPixelFormat;
	}

	auto UnusedResourceIndex = UnusedRenderTargetResources.IndexOfByPredicate([RequiredResourceSettings](const FDisplayClusterRenderTargetResource* ResourceIt)
	{
		return ResourceIt && ResourceIt->IsResourceSettingsEqual(RequiredResourceSettings);
	});

	if (UnusedResourceIndex != INDEX_NONE)
	{
		// Use exist resource again
		FDisplayClusterRenderTargetResource* ExistResource = UnusedRenderTargetResources[UnusedResourceIndex];
		UnusedRenderTargetResources[UnusedResourceIndex] = nullptr;

		RenderTargetResources.Add(ExistResource);
		return ExistResource;
	}

	// Create new resource:
	FDisplayClusterRenderTargetResource* NewResource = new FDisplayClusterRenderTargetResource(RequiredResourceSettings);
	RenderTargetResources.Add(NewResource);
	CreatedRenderTargetResources.Add(NewResource);

	return NewResource;
}

void FDisplayClusterRenderTargetResourcesPool::FinishReallocateRenderTargetResources()
{
	if (pRenderTargetResourceSettings != nullptr)
	{
		delete pRenderTargetResourceSettings;
		pRenderTargetResourceSettings = nullptr;

		ReleaseRenderTargetResources(UnusedRenderTargetResources);
		UnusedRenderTargetResources.Empty();

		if (CreatedRenderTargetResources.Num() > 0)
		{
			// Send all new resource to render thread in single cmd
			ENQUEUE_RENDER_COMMAND(InitRenderTargetCommand)(
				[NewResources = std::move(CreatedRenderTargetResources)](FRHICommandListImmediate& RHICmdList)
			{
				for (FDisplayClusterRenderTargetResource* NewResourceIt : NewResources)
				{
					NewResourceIt->InitResource();
				}
			});

			CreatedRenderTargetResources.Empty();
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------------------------

bool FDisplayClusterRenderTargetResourcesPool::BeginReallocateTextureResources(class FViewport* InViewport)
{
	if (pTextureResourceSettings == nullptr)
	{
		pTextureResourceSettings = new FDisplayClusterViewportResourceSettings(InViewport);

		UnusedTextureResources = TextureResources;
		TextureResources.Empty();

		return true;
	}

	return false;
}

FDisplayClusterTextureResource* FDisplayClusterRenderTargetResourcesPool::AllocateTextureResource(const FIntPoint& InSize, bool bIsRenderTargetable, enum EPixelFormat CustomPixelFormat, int InNumMips)
{
	if (pTextureResourceSettings == nullptr || !IsTextureSizeValid(InSize))
	{
		return nullptr;
	}

	FDisplayClusterViewportResourceSettings RequiredResourceSettings = *pTextureResourceSettings;
	RequiredResourceSettings.Size = InSize;
	RequiredResourceSettings.bIsRenderTargetable = bIsRenderTargetable;
	RequiredResourceSettings.NumMips = InNumMips;

	if (CustomPixelFormat != PF_Unknown)
	{
		RequiredResourceSettings.Format = CustomPixelFormat;
	}

	auto UnusedResourceIndex = UnusedTextureResources.IndexOfByPredicate([RequiredResourceSettings](const FDisplayClusterTextureResource* ResourceIt)
	{
		return ResourceIt && ResourceIt->IsResourceSettingsEqual(RequiredResourceSettings);
	});

	if (UnusedResourceIndex != INDEX_NONE)
	{
		// Use exist resource again
		FDisplayClusterTextureResource* ExistResource = UnusedTextureResources[UnusedResourceIndex];
		UnusedTextureResources[UnusedResourceIndex] = nullptr;

		TextureResources.Add(ExistResource);
		return ExistResource;
	}

	// Create new resource:
	FDisplayClusterTextureResource* NewResource = new FDisplayClusterTextureResource(RequiredResourceSettings);
	TextureResources.Add(NewResource);
	CreatedTextureResources.Add(NewResource);

	return NewResource;
}

void FDisplayClusterRenderTargetResourcesPool::FinishReallocateTextureResources()
{
	if (pTextureResourceSettings != nullptr)
	{
		delete pTextureResourceSettings;
		pTextureResourceSettings = nullptr;

		ReleaseTextureResources(UnusedTextureResources);
		UnusedTextureResources.Empty();

		// Send all new resource to render thread in single cmd
		if (CreatedTextureResources.Num() > 0)
		{
			ENQUEUE_RENDER_COMMAND(InitTextureResourceCommand)(
				[NewResources = std::move(CreatedTextureResources)](FRHICommandListImmediate& RHICmdList)
			{
				for (FDisplayClusterTextureResource* NewResourceIt : NewResources)
				{
					NewResourceIt->InitDynamicRHI();
				}
			});

			CreatedTextureResources.Empty();
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------------------------

void FDisplayClusterRenderTargetResourcesPool::ReleaseRenderTargetResources(TArray<FDisplayClusterRenderTargetResource*>& InOutResources)
{
	TArray<FDisplayClusterRenderTargetResource*> ExistResourcesForRemove;
	for (FDisplayClusterRenderTargetResource* It : InOutResources)
	{
		if (It != nullptr)
		{
			ExistResourcesForRemove.Add(It);
		}
	}
	InOutResources.Empty();

	if (ExistResourcesForRemove.Num() > 0)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseRenderTargetResourceCommand)(
			[RemovedResources = std::move(ExistResourcesForRemove)](FRHICommandListImmediate& RHICmdList)
		{
			for (FDisplayClusterRenderTargetResource* RemovedResourceIt : RemovedResources)
			{
				RemovedResourceIt->ReleaseResource();
				delete RemovedResourceIt;
			}
		});
	}
}

void FDisplayClusterRenderTargetResourcesPool::ReleaseTextureResources(TArray<FDisplayClusterTextureResource*>& InOutResources)
{
	TArray<FDisplayClusterTextureResource*> ExistResourcesForRemove;
	for (FDisplayClusterTextureResource* It : InOutResources)
	{
		if (It != nullptr)
		{
			ExistResourcesForRemove.Add(It);
		}
	}
	InOutResources.Empty();

	if (ExistResourcesForRemove.Num() > 0)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseTextureResourceCommand)(
			[RemovedResources = std::move(ExistResourcesForRemove)](FRHICommandListImmediate& RHICmdList)
		{
			for (FDisplayClusterTextureResource* RemovedResourceIt : RemovedResources)
			{
				RemovedResourceIt->ReleaseRHI();
				delete RemovedResourceIt;
			}
		});
	}
}
