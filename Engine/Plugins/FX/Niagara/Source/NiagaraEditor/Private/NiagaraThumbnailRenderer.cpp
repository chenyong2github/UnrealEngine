// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraThumbnailRenderer.h"
#include "CanvasTypes.h"
#include "NiagaraEmitter.h"
#include "NiagaraSystem.h"

bool UNiagaraThumbnailRendererBase::CanVisualizeAsset(UObject* Object)
{
	return GetThumbnailTextureFromObject(Object) != nullptr;
}

void UNiagaraThumbnailRendererBase::GetThumbnailSize(UObject* Object, float Zoom, uint32& OutWidth, uint32& OutHeight) const
{
	UTexture2D* ObjectTexture = GetThumbnailTextureFromObject(Object);
	if (ObjectTexture != nullptr)
	{
		OutWidth = Zoom * ObjectTexture->GetSizeX();
		OutHeight = Zoom * ObjectTexture->GetSizeY();
	}
	else
	{
		OutWidth = 0;
		OutHeight = 0;
	}
}

void UNiagaraThumbnailRendererBase::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	UTexture2D* ObjectTexture = GetThumbnailTextureFromObject(Object);
	if (ObjectTexture != nullptr)
	{
		Super::Draw(ObjectTexture, X, Y, Width, Height, RenderTarget, Canvas, bAdditionalViewFamily);
	}
}

UTexture2D* UNiagaraEmitterThumbnailRenderer::GetThumbnailTextureFromObject(UObject* Object) const
{
	UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Object);
	if (Emitter != nullptr)
	{
		return Emitter->ThumbnailImage;
	}
	return nullptr;
}

UTexture2D* UNiagaraSystemThumbnailRenderer::GetThumbnailTextureFromObject(UObject* Object) const
{
	UNiagaraSystem* System = Cast<UNiagaraSystem>(Object);
	if (System != nullptr)
	{
		return System->ThumbnailImage;
	}
	return nullptr;
}