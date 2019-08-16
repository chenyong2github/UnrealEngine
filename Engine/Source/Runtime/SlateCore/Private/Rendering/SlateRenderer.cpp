// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Rendering/SlateRenderer.h"
#include "Textures/TextureAtlas.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Widgets/SWindow.h"

DEFINE_STAT(STAT_SlatePreFullBufferRTTime);

/* FSlateFontCacheProvider interface
 *****************************************************************************/

FSlateFontServices::FSlateFontServices(TSharedRef<class FSlateFontCache> InGameThreadFontCache, TSharedRef<class FSlateFontCache> InRenderThreadFontCache)
	: GameThreadFontCache(InGameThreadFontCache)
	, RenderThreadFontCache(InRenderThreadFontCache)
	, GameThreadFontMeasure(FSlateFontMeasure::Create(GameThreadFontCache))
	, RenderThreadFontMeasure((GameThreadFontCache == RenderThreadFontCache) ? GameThreadFontMeasure : FSlateFontMeasure::Create(RenderThreadFontCache))
{
	UE_LOG(LogSlate, Log, TEXT("SlateFontServices - WITH_FREETYPE: %d, WITH_HARFBUZZ: %d"), WITH_FREETYPE, WITH_HARFBUZZ);
}


TSharedRef<FSlateFontCache> FSlateFontServices::GetFontCache() const
{
	const ESlateTextureAtlasThreadId AtlasThreadId = GetCurrentSlateTextureAtlasThreadId();
	check(AtlasThreadId != ESlateTextureAtlasThreadId::Unknown);

	if (AtlasThreadId == ESlateTextureAtlasThreadId::Game)
	{
		return GameThreadFontCache;
	}
	else
	{
		return RenderThreadFontCache;
	}
}


TSharedRef<class FSlateFontMeasure> FSlateFontServices::GetFontMeasureService() const
{
	const ESlateTextureAtlasThreadId AtlasThreadId = GetCurrentSlateTextureAtlasThreadId();
	check(AtlasThreadId != ESlateTextureAtlasThreadId::Unknown);

	if (AtlasThreadId == ESlateTextureAtlasThreadId::Game)
	{
		return GameThreadFontMeasure;
	}
	else
	{
		return RenderThreadFontMeasure;
	}
}


void FSlateFontServices::FlushFontCache(const FString& FlushReason)
{
	const ESlateTextureAtlasThreadId AtlasThreadId = GetCurrentSlateTextureAtlasThreadId();
	check(AtlasThreadId != ESlateTextureAtlasThreadId::Unknown);

	if (AtlasThreadId == ESlateTextureAtlasThreadId::Game)
	{
		return FlushGameThreadFontCache(FlushReason);
	}
	else
	{
		return FlushRenderThreadFontCache(FlushReason);
	}
}


void FSlateFontServices::FlushGameThreadFontCache(const FString& FlushReason)
{
	GameThreadFontCache->RequestFlushCache(FlushReason);
	GameThreadFontMeasure->FlushCache();
}


void FSlateFontServices::FlushRenderThreadFontCache(const FString& FlushReason)
{
	RenderThreadFontCache->RequestFlushCache(FlushReason);
	RenderThreadFontMeasure->FlushCache();
}


void FSlateFontServices::ReleaseResources()
{
	GameThreadFontCache->ReleaseResources();

	if (GameThreadFontCache != RenderThreadFontCache)
	{
		RenderThreadFontCache->ReleaseResources();
	}
}


/* FSlateRenderer interface
 *****************************************************************************/

bool FSlateRenderer::IsViewportFullscreen( const SWindow& Window ) const
{
	checkSlow( IsThreadSafeForSlateRendering() );

	bool bFullscreen = false;

	if (FPlatformProperties::SupportsWindowedMode())
	{
		if( GIsEditor)
		{
			bFullscreen = false;
		}
		else
		{
			bFullscreen = Window.GetWindowMode() == EWindowMode::Fullscreen;
		}
	}
	else
	{
		bFullscreen = true;
	}

	return bFullscreen;
}


ISlateAtlasProvider* FSlateRenderer::GetTextureAtlasProvider()
{
	return nullptr;
}


ISlateAtlasProvider* FSlateRenderer::GetFontAtlasProvider()
{
	return &SlateFontServices->GetGameThreadFontCache().Get();
}

void FSlateRenderer::DestroyCachedFastPathRenderingData(struct FSlateCachedFastPathRenderingData* FastPathRenderingData)
{
	check(FastPathRenderingData);
	delete FastPathRenderingData;
}

void FSlateRenderer::DestroyCachedFastPathElementData(struct FSlateCachedElementData* ElementData)
{
	check(ElementData);
	delete ElementData;
}

/* Global functions
 *****************************************************************************/

bool IsThreadSafeForSlateRendering()
{
	return ( ( GSlateLoadingThreadId != 0 ) || IsInGameThread() );
}

bool DoesThreadOwnSlateRendering()
{
	if ( IsInGameThread() )
	{
		return GSlateLoadingThreadId == 0;
	}
	else
	{
		return FPlatformTLS::GetCurrentThreadId() == GSlateLoadingThreadId;
	}

	return false;
}
