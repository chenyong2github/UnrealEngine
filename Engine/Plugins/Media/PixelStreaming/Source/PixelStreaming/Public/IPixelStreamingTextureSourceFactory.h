// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"
#include "Templates/Function.h"
#include "PixelStreamingTextureSource.h"

/*
 * A factory where "TextureSourceCreators" are registered as lambdas with unique FNames.
 * The premise is that outside implementers can register texture source creators using the Pixel Streaming module.
 */
class PIXELSTREAMING_API IPixelStreamingTextureSourceFactory
{
public:
	virtual ~IPixelStreamingTextureSourceFactory(){};

	/**
	 * Create a TextureSource based on a known named source, e.g. Backbuffer.
	 * These sources can be used to capture raw frames/textures and use them as desired - typically wrapping them as WebRTC video sources.
	 * @param ScoureType - The type of a registered texture source.
	 * @return The texture source as a TUniquePtr - this means you own it now.
	 */
	virtual TUniquePtr<FPixelStreamingTextureSource> CreateTextureSource(FName SourceType) = 0;

	/**
	 * Register a texture source type that can be made active or created using the other functions on this module.
	 * @param SourceType - The unique name to identify this type of texture source.
	 * @param CreatorFunc - A lambda that will create the texture source of this type.
	 */
	virtual void RegisterTextureSourceType(FName SourceType, TFunction<TUniquePtr<FPixelStreamingTextureSource>()> CreatorFunc) = 0;

	/**
	 * Unregister this texture source type from being created.
	 * @param SourceType - The type to unregister and remove.
	 */
	virtual void UnregisterTextureSourceType(FName SourceType) = 0;
};
