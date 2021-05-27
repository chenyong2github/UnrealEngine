// Copyright Epic Games Tools LLC
//   Licenced under the Unreal Engine EULA 

#pragma once

#include "BinkMediaPlayer.h"

struct FBinkMediaPlayerEditorTexture : public FTickableGameObject 
{
	FBinkMediaPlayerEditorTexture( FSlateTexture2DRHIRef* InSlateTexture, UBinkMediaPlayer *InMediaPlayer )
		: FTickableGameObject()
		, SlateTexture(InSlateTexture)
		, MediaPlayer(InMediaPlayer)
	{
	}

	~FBinkMediaPlayerEditorTexture() 
	{
	}

	virtual void Tick( float DeltaTime ) override 
	{
		ENQUEUE_RENDER_COMMAND(BinkEditorUpdateTexture)([MediaPlayer=MediaPlayer,SlateTexture=SlateTexture](FRHICommandListImmediate& RHICmdList) 
		{
			if (!SlateTexture->IsInitialized())
			{
				SlateTexture->InitResource();
			}
			FTexture2DRHIRef tex = SlateTexture->GetTypedResource();
			if (!tex.GetReference())
			{
				return;
			}

			uint32 width = tex->GetSizeX();
			uint32 height = tex->GetSizeY();
			void* nativePtr = tex->GetNativeResource();
			FRHIRenderPassInfo RPInfo(SlateTexture->GetRHIRef(), ERenderTargetActions::Load_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("RenderBink"));
			RHICmdList.SetViewport(0, 0, 0.0f, width, height, 1.0f);
			RHICmdList.SubmitCommandsHint();
			MediaPlayer->UpdateTexture(RHICmdList, static_cast<FTextureRHIRef>(tex), nativePtr, width, height, true, true, 80, 1, false, false);
			RHICmdList.SubmitCommandsHint();
			RHICmdList.Transition(FRHITransitionInfo(SlateTexture->GetRHIRef(), ERHIAccess::Unknown, ERHIAccess::EReadable));
			RHICmdList.EndRenderPass();
		});
	}
	
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FBinkMediaPlayerEditorTexture, STATGROUP_Tickables); }
	virtual bool IsTickable() const override { return MediaPlayer != nullptr; }
	virtual bool IsTickableInEditor() const override { return true; }

	FSlateTexture2DRHIRef* SlateTexture;
	UBinkMediaPlayer *MediaPlayer;
};
