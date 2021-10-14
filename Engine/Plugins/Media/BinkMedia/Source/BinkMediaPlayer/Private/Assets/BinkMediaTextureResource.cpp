// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

#include "BinkMediaTextureResource.h"
#include "BinkMediaPlayerPCH.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Runtime/Launch/Resources/Version.h"
#include "BinkMediaPlayer.h"
#include "BinkMediaTexture.h"

void FBinkMediaTextureResource::InitDynamicRHI() 
{
	int w = Owner->GetSurfaceWidth() > 0 ? Owner->GetSurfaceWidth() : 1;
	int h = Owner->GetSurfaceHeight() > 0 ? Owner->GetSurfaceHeight() : 1;
	// Enforce micro-tile restrictions for render targets.
	w = (w + 7) & -8;
	h = (h + 7) & -8;

	// Create the RHI texture. Only one mip is used and the texture is targetable or resolve.
	ETextureCreateFlags TexCreateFlags = Owner->SRGB ? TexCreate_SRGB : TexCreate_None;
	FRHIResourceCreateInfo CreateInfo;
	
	if (bink_force_pixel_format != PF_Unknown) 
	{
		PixelFormat = bink_force_pixel_format;
	}

	// Some platforms don't support srgb 10.10.10.2 formats
	if (PixelFormat == PF_A2B10G10R10) 
	{
		TexCreateFlags = TexCreate_None;
	}

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
	FString DebugNameString = TEXT("Bink:");
	DebugNameString += Owner->GetName();
	CreateInfo.DebugName = *DebugNameString;
#endif // ARK_EXTRA_RESOURCE_NAMES

	TRefCountPtr<FRHITexture2D> Texture2DRHI;
	RHICreateTargetableShaderResource2D(
		w, h,
		PixelFormat,
		1,
		TexCreateFlags,
		TexCreate_RenderTargetable,
		false,
		CreateInfo,
		RenderTargetTextureRHI,
		Texture2DRHI
	);

	TextureRHI = (FTextureRHIRef&)Texture2DRHI;
	
	// Don't bother updating if its not a valid video
	if (Owner->GetSurfaceWidth() && Owner->GetSurfaceHeight()) 
	{
		AddToDeferredUpdateList(false);
	}

	// Create the sampler state RHI resource.
	FSamplerStateInitializerRHI SamplerStateInitializer(
		(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(Owner),
		Owner->AddressX == TA_Wrap ? AM_Wrap : (Owner->AddressX == TA_Clamp ? AM_Clamp : AM_Mirror),
		Owner->AddressY == TA_Wrap ? AM_Wrap : (Owner->AddressY == TA_Clamp ? AM_Clamp : AM_Mirror),
		AM_Wrap
	);

	SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

	RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, RenderTargetTextureRHI.GetReference());

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::Clear_Store);
		TransitionRenderPassTargets(RHICmdList, RPInfo);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
		RHICmdList.EndRenderPass();
		RHICmdList.SetViewport(0, 0, 0, w, h, 1);
		RHICmdList.Transition(FRHITransitionInfo(TextureRHI.GetReference(), ERHIAccess::Unknown, ERHIAccess::EReadable));

		// Work-around for UE4 bug when playing a chunk loaded video while also streaming a movie
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
		RHIFlushResources();
		RHICmdList.SubmitCommandsHint();
		FPlatformMisc::MemoryBarrier();
	}
}

void FBinkMediaTextureResource::ReleaseDynamicRHI() 
{
	ReleaseRHI();
	RenderTargetTextureRHI.SafeRelease();
	RemoveFromDeferredUpdateList();

	// Work-around for UE4 bug when playing a chunk loaded video while also streaming a movie
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
	RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);
	RHIFlushResources();
	RHICmdList.SubmitCommandsHint();
	FPlatformMisc::MemoryBarrier();
}

void FBinkMediaTextureResource::UpdateDeferredResource(FRHICommandListImmediate& RHICmdList, bool bClearRenderTarget) 
{
	auto Player = Owner->MediaPlayer;
	if (!Player || (!Player->IsPlaying() && !Player->IsPaused()) || !TextureRHI) 
	{
		return;
	}
	FRHITexture2D * tex = TextureRHI->GetTexture2D();
	if (!tex) 
	{
		return;
	}
	uint32 width = tex->GetSizeX();
	uint32 height = tex->GetSizeY();
	bool is_hdr = PixelFormat != PF_B8G8R8A8;
	Player->UpdateTexture(RHICmdList, TextureRHI, tex->GetNativeResource(), width, height, false, Owner->Tonemap, Owner->OutputNits, Owner->Alpha, Owner->DecodeSRGB, is_hdr);
}

void FBinkMediaTextureResource::Clear() 
{
	int w = Owner->GetSurfaceWidth() > 0 ? Owner->GetSurfaceWidth() : 1;
	int h = Owner->GetSurfaceHeight() > 0 ? Owner->GetSurfaceHeight() : 1;

	// Enforce micro-tile restrictions for render targets.
	w = (w + 7) & -8;
	h = (h + 7) & -8;

	FTexture2DRHIRef ref = RenderTargetTextureRHI;
	FTextureRHIRef ref2 = TextureRHI;
	ENQUEUE_RENDER_COMMAND(BinkMediaPlayer_Draw)([ref,ref2,w,h](FRHICommandListImmediate& RHICmdList) 
	{ 
		FRHIRenderPassInfo RPInfo(ref2, ERenderTargetActions::Clear_Store);
		TransitionRenderPassTargets(RHICmdList, RPInfo);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
		RHICmdList.EndRenderPass();
		RHICmdList.SetViewport(0, 0, 0, w, h, 1);
		RHICmdList.Transition(FRHITransitionInfo(ref2.GetReference(), ERHIAccess::Unknown, ERHIAccess::EReadable));
	});
}

