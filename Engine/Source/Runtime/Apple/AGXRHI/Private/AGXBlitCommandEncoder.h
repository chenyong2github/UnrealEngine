// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <Metal/Metal.h>
#include "AGXDebugCommandEncoder.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
NS_ASSUME_NONNULL_BEGIN

@class FAGXDebugCommandBuffer;
@class FAGXDebugBlitCommandEncoder;

class FAGXBlitCommandEncoderDebugging : public FAGXCommandEncoderDebugging
{
public:
	FAGXBlitCommandEncoderDebugging();
	FAGXBlitCommandEncoderDebugging(mtlpp::BlitCommandEncoder& Encoder, FAGXCommandBufferDebugging& Buffer);
	FAGXBlitCommandEncoderDebugging(FAGXDebugCommandEncoder* handle);
	
	static FAGXBlitCommandEncoderDebugging Get(mtlpp::BlitCommandEncoder& Buffer);
	
	void InsertDebugSignpost(ns::String const& Label);
	void PushDebugGroup(ns::String const& Group);
	void PopDebugGroup();
	
	void EndEncoder();
	
#if PLATFORM_MAC
	void Synchronize(mtlpp::Resource const& resource);
	
	void Synchronize(FAGXTexture const& texture, NSUInteger slice, NSUInteger level);
#endif
	
	void Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin);
	
	void Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, NSUInteger sourceBytesPerRow, NSUInteger sourceBytesPerImage, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin);
	
	void Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, NSUInteger sourceBytesPerRow, NSUInteger sourceBytesPerImage, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin, mtlpp::BlitOption options);
	
	void Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger destinationBytesPerRow, NSUInteger destinationBytesPerImage);
	
	void Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger destinationBytesPerRow, NSUInteger destinationBytesPerImage, mtlpp::BlitOption options);
	
	void GenerateMipmaps(FAGXTexture const& texture);
	
	void Fill(FAGXBuffer const& buffer, ns::Range const& range, uint8_t value);
	
	void Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger size);
};

NS_ASSUME_NONNULL_END
#endif
