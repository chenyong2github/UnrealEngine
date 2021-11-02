// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareDisplayExtension.h"
#include "TextureShareModule.h"
#include "TextureShareLog.h"

#include "CoreGlobals.h"

FTextureShareDisplayExtension::FTextureShareDisplayExtension(const FAutoRegister& AutoRegister, FTextureShareDisplayManager& InTextureShareDisplayManager, FViewport* AssociatedViewport)
	: FSceneViewExtensionBase(AutoRegister)
	, TextureShareDisplayManager(InTextureShareDisplayManager)
	, LinkedViewport(AssociatedViewport)
{
}

void FTextureShareDisplayExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
	TextureShareDisplayManager.OnBeginRenderViewFamily(InViewFamily);
}

void FTextureShareDisplayExtension::PreRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	TextureShareDisplayManager.OnPreRenderViewFamily_RenderThread(RHICmdList, InViewFamily);
}

void FTextureShareDisplayExtension::PostRenderViewFamily_RenderThread(FRHICommandListImmediate& RHICmdList, FSceneViewFamily& InViewFamily)
{
	TextureShareDisplayManager.OnPostRenderViewFamily_RenderThread(RHICmdList, InViewFamily);
}

bool FTextureShareDisplayExtension::IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const
{
	return (LinkedViewport == Context.Viewport);
}
