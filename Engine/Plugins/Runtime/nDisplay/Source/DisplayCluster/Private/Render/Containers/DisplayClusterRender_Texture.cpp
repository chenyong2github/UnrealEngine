// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_Texture.h"
#include "RenderingThread.h"

int32 GDisplayClusterRender_TextureCacheEnable = 1;
static FAutoConsoleVariableRef CVarDisplayClusterRender_TextureCacheEnable(
	TEXT("nDisplay.cache.Texture.enable"),
	GDisplayClusterRender_TextureCacheEnable,
	TEXT("Enable the use of a named texture cache.\n"),
	ECVF_Default
);

int32 GDisplayClusterRender_TextureCacheTimeOutInFrames = 5 * 60 * 60; // Timeout is 5 minutes (for 60 frames per second)
static FAutoConsoleVariableRef CVarDisplayClusterRenderCached_TextureCacheTimeOutInFrames(
	TEXT("nDisplay.cache.Texture.TimeOutInFrames"),
	GDisplayClusterRender_TextureCacheTimeOutInFrames,
	TEXT("The timeout value in frames for the named texture cache.\n")
	TEXT("-1 - disable timeout.\n"),
	ECVF_Default
);

//---------------------------------------------------------------------------------------
// FDisplayClusterRender_Texture
//---------------------------------------------------------------------------------------
FDisplayClusterRender_Texture::FDisplayClusterRender_Texture(const FString& InUniqueTextureName)
	: UniqueTextureName(InUniqueTextureName)
{ }

FDisplayClusterRender_Texture::~FDisplayClusterRender_Texture()
{
	if (PrivateResource.IsValid())
	{
		PrivateResource->ReleaseRenderResource();
		PrivateResource.Reset();
	}

	if (PrivateResourceRenderThread.IsValid())
	{
		PrivateResourceRenderThread->ReleaseRenderResource();
		PrivateResourceRenderThread.Reset();
	}
}

void FDisplayClusterRender_Texture::CreateTexture(const void* InTextureData, const uint32 InComponentDepth, const uint32 InBytesPerComponent, uint32_t InWidth, uint32_t InHeight, bool bInHasCPUAccess)
{
	// Release the existing texture resource.
	ReleaseResource();

	//Dedicated servers have no texture internals
	if (FApp::CanEverRender())
	{
		// Create a new texture resource.
		TSharedPtr<FDisplayClusterRender_TextureResource, ESPMode::ThreadSafe> NewResource = MakeShared<FDisplayClusterRender_TextureResource, ESPMode::ThreadSafe>(InTextureData, InComponentDepth, InBytesPerComponent, InWidth, InHeight, bInHasCPUAccess);
		if (NewResource.IsValid())
		{
			NewResource->InitializeRenderResource();
		}

		SetResource(NewResource);
	}
}

const TSharedPtr<FDisplayClusterRender_TextureResource>& FDisplayClusterRender_Texture::GetResource() const
{
	if (IsInActualRenderingThread() || IsInRHIThread())
	{
		return PrivateResourceRenderThread;
	}
	return PrivateResource;
}

void FDisplayClusterRender_Texture::SetResource(const TSharedPtr<FDisplayClusterRender_TextureResource>& InResource)
{
	check(!IsInActualRenderingThread() && !IsInRHIThread());

	// Each PrivateResource value must be updated in it's own thread because any
	// rendering code trying to access the Resource
	// crash if it suddenly sees nullptr or a new resource that has not had it's InitRHI called.

	PrivateResource = InResource;
	ENQUEUE_RENDER_COMMAND(DisplayCluster_SetResourceRenderThread)([This = SharedThis(this), InResource](FRHICommandListImmediate& RHICmdList)
	{
		This->PrivateResourceRenderThread = InResource;
	});
}

void FDisplayClusterRender_Texture::ReleaseResource()
{
	if (PrivateResource)
	{
		// Free the resource.
		SetResource(nullptr);
	}
}

int32 FDisplayClusterRender_Texture::GetDataCacheTimeOutInFrames()
{
	return FMath::Max(0, GDisplayClusterRender_TextureCacheTimeOutInFrames);
}

bool FDisplayClusterRender_Texture::IsDataCacheEnabled()
{
	return GDisplayClusterRender_TextureCacheEnable != 0;
}
