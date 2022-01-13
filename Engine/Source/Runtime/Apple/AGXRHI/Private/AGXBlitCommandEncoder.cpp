// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXBlitCommandEncoder.cpp: AGX command encoder wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"

#include "AGXBlitCommandEncoder.h"
#include "AGXCommandBuffer.h"
#include "AGXFence.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
extern int32 GAGXRuntimeDebugLevel;

@interface FAGXDebugBlitCommandEncoder : FAGXDebugCommandEncoder
{
	@public
	id<MTLBlitCommandEncoder> Inner;
	FAGXCommandBufferDebugging Buffer;
}
/** Initialise the wrapper with the provided command-buffer. */
-(id)initWithEncoder:(id<MTLBlitCommandEncoder>)Encoder andCommandBuffer:(FAGXCommandBufferDebugging const&)Buffer;

@end

@implementation FAGXDebugBlitCommandEncoder

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDebugBlitCommandEncoder)

-(id)initWithEncoder:(id<MTLBlitCommandEncoder>)Encoder andCommandBuffer:(FAGXCommandBufferDebugging const&)SourceBuffer
{
	id Self = [super init];
	if (Self)
	{
        Inner = Encoder;
		Buffer = SourceBuffer;
	}
	return Self;
}

-(void)dealloc
{
	[super dealloc];
}

@end

FAGXBlitCommandEncoderDebugging::FAGXBlitCommandEncoderDebugging()
{
	
}
FAGXBlitCommandEncoderDebugging::FAGXBlitCommandEncoderDebugging(mtlpp::BlitCommandEncoder& Encoder, FAGXCommandBufferDebugging& Buffer)
: FAGXCommandEncoderDebugging((FAGXDebugCommandEncoder*)[[[FAGXDebugBlitCommandEncoder alloc] initWithEncoder:Encoder.GetPtr() andCommandBuffer:Buffer] autorelease])
{
	Buffer.BeginBlitCommandEncoder([NSString stringWithFormat:@"Blit: %@", Encoder.GetLabel().GetPtr()]);
	Encoder.SetAssociatedObject((void const*)&FAGXBlitCommandEncoderDebugging::Get, (FAGXCommandEncoderDebugging const&)*this);
}
FAGXBlitCommandEncoderDebugging::FAGXBlitCommandEncoderDebugging(FAGXDebugCommandEncoder* handle)
: FAGXCommandEncoderDebugging((FAGXDebugCommandEncoder*)handle)
{
	
}

FAGXBlitCommandEncoderDebugging FAGXBlitCommandEncoderDebugging::Get(mtlpp::BlitCommandEncoder& Encoder)
{
	return Encoder.GetAssociatedObject<FAGXBlitCommandEncoderDebugging>((void const*)&FAGXBlitCommandEncoderDebugging::Get);
}

void FAGXBlitCommandEncoderDebugging::InsertDebugSignpost(ns::String const& Label)
{
	((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.InsertDebugSignpost(Label);
}
void FAGXBlitCommandEncoderDebugging::PushDebugGroup(ns::String const& Group)
{
	((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.PushDebugGroup(Group);
}
void FAGXBlitCommandEncoderDebugging::PopDebugGroup()
{
	((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.PopDebugGroup();
}

void FAGXBlitCommandEncoderDebugging::EndEncoder()
{
	((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.EndCommandEncoder();
}

#if PLATFORM_MAC
void FAGXBlitCommandEncoderDebugging::Synchronize(mtlpp::Resource const& resource)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Synchronize(FAGXTexture const& texture, NSUInteger slice, NSUInteger level)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}
#endif

void FAGXBlitCommandEncoderDebugging::Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, NSUInteger sourceBytesPerRow, NSUInteger sourceBytesPerImage, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, NSUInteger sourceBytesPerRow, NSUInteger sourceBytesPerImage, mtlpp::Size const& sourceSize, FAGXTexture const& destinationTexture, NSUInteger destinationSlice, NSUInteger destinationLevel, mtlpp::Origin const& destinationOrigin, mtlpp::BlitOption options)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger destinationBytesPerRow, NSUInteger destinationBytesPerImage)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Copy(FAGXTexture const& sourceTexture, NSUInteger sourceSlice, NSUInteger sourceLevel, mtlpp::Origin const& sourceOrigin, mtlpp::Size const& sourceSize, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger destinationBytesPerRow, NSUInteger destinationBytesPerImage, mtlpp::BlitOption options)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::GenerateMipmaps(FAGXTexture const& texture)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Fill(FAGXBuffer const& buffer, ns::Range const& range, uint8_t value)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXBlitCommandEncoderDebugging::Copy(FAGXBuffer const& sourceBuffer, NSUInteger sourceOffset, FAGXBuffer const& destinationBuffer, NSUInteger destinationOffset, NSUInteger size)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugBlitCommandEncoder*)m_ptr)->Buffer.Blit([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		default:
		{
			break;
		}
	}
#endif
}

#endif
