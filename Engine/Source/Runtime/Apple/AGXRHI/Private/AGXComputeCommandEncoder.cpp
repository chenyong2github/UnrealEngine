// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXComputeCommandEncoder.cpp: AGX command encoder wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"

#include "AGXComputeCommandEncoder.h"
#include "AGXCommandBuffer.h"
#include "AGXFence.h"
#include "AGXPipeline.h"

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
NS_ASSUME_NONNULL_BEGIN

#if METAL_DEBUG_OPTIONS
static NSString* GAGXDebugComputeShader = @"#include <metal_stdlib>\n"
@"using namespace metal;\n"
@"kernel void WriteCommandIndexCS(constant uint* Input [[ buffer(0) ]], device atomic_uint* Output [[ buffer(1) ]])\n"
@"{\n"
@"	atomic_store_explicit(Output, Input[0], memory_order_relaxed);\n"
@"}\n";

static id <MTLComputePipelineState> GetDebugComputeShaderState(id<MTLDevice> Device)
{
	static id<MTLComputePipelineState> State = nil;
	if (!State)
	{
		id<MTLLibrary> Lib = [Device newLibraryWithSource:GAGXDebugComputeShader options:nil error:nullptr];
		check(Lib);
		id<MTLFunction> Func = [Lib newFunctionWithName:@"WriteCommandIndexCS"];
		check(Func);
		State = [Device newComputePipelineStateWithFunction:Func error:nil];
		[Func release];
		[Lib release];
	}
	check(State);
	return State;
}
#endif

@interface FAGXDebugComputeCommandEncoder : FAGXDebugCommandEncoder
{
@public
	id<MTLComputeCommandEncoder> Inner;
	FAGXCommandBufferDebugging Buffer;
	FAGXShaderPipeline* Pipeline;

#pragma mark - Private Member Variables -
#if METAL_DEBUG_OPTIONS
	FAGXDebugShaderResourceMask ResourceMask;
    FAGXDebugBufferBindings ShaderBuffers;
    FAGXDebugTextureBindings ShaderTextures;
    FAGXDebugSamplerBindings ShaderSamplers;
#endif
}
/** Initialise the wrapper with the provided command-buffer. */
-(id)initWithEncoder:(id<MTLComputeCommandEncoder>)Encoder andCommandBuffer:(FAGXCommandBufferDebugging const&)Buffer;
@end

@implementation FAGXDebugComputeCommandEncoder

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDebugComputeCommandEncoder)

-(id)initWithEncoder:(id<MTLComputeCommandEncoder>)Encoder andCommandBuffer:(FAGXCommandBufferDebugging const&)SourceBuffer
{
	id Self = [super init];
	if (Self)
	{
        Inner = Encoder;
		Buffer = SourceBuffer;
        Pipeline = nil;
	}
	return Self;
}

-(void)dealloc
{
	[Pipeline release];
	[super dealloc];
}

@end

#if METAL_DEBUG_OPTIONS
void FAGXComputeCommandEncoderDebugging::InsertDebugDispatch()
{
//	switch (Buffer->DebugLevel)
//	{
//		case EAGXDebugLevelConditionalSubmit:
//		case EAGXDebugLevelWaitForComplete:
//		case EAGXDebugLevelLogOperations:
//		case EAGXDebugLevelValidation:
//		{
//			uint32 const Index = Buffer->DebugCommands.Num();
//			[Inner setBytes:&Index length:sizeof(Index) atIndex:0];
//			[Inner setBuffer:Buffer->DebugInfoBuffer offset:0 atIndex:1];
//			[Inner setComputePipelineState:GetDebugComputeShaderState(Inner.device)];
//			
//			[Inner dispatchThreadgroups:mtlpp::Size const&Make(1, 1, 1) threadsPerThreadgroup:mtlpp::Size const&Make(1, 1, 1)];
//
//			if (Pipeline && Pipeline->ComputePipelineState)
//			{
//				[Inner setComputePipelineState:Pipeline->ComputePipelineState];
//			}
//			
//			if (ShaderBuffers.Buffers[0])
//			{
//				[Inner setBuffer:ShaderBuffers.Buffers[0] offset:ShaderBuffers.Offsets[0] atIndex:0];
//			}
//			else if (ShaderBuffers.Bytes[0])
//			{
//				[Inner setBytes:ShaderBuffers.Bytes[0] length:ShaderBuffers.Offsets[0] atIndex:0];
//			}
//			
//			if (ShaderBuffers.Buffers[1])
//			{
//				[Inner setBuffer:ShaderBuffers.Buffers[1] offset:ShaderBuffers.Offsets[1] atIndex:1];
//			}
//			else if (ShaderBuffers.Bytes[1])
//			{
//				[Inner setBytes:ShaderBuffers.Bytes[1] length:ShaderBuffers.Offsets[1] atIndex:1];
//			}
//		}
//		default:
//		{
//			break;
//		}
//	}
}
#endif

FAGXComputeCommandEncoderDebugging::FAGXComputeCommandEncoderDebugging()
{
	
}
FAGXComputeCommandEncoderDebugging::FAGXComputeCommandEncoderDebugging(mtlpp::ComputeCommandEncoder& Encoder, FAGXCommandBufferDebugging& Buffer)
: FAGXCommandEncoderDebugging((FAGXDebugCommandEncoder*)[[[FAGXDebugComputeCommandEncoder alloc] initWithEncoder:Encoder.GetPtr() andCommandBuffer:Buffer] autorelease])
{
	Buffer.BeginComputeCommandEncoder([NSString stringWithFormat:@"Compute: %@", Encoder.GetLabel().GetPtr()]);
	Encoder.SetAssociatedObject((void const*)&FAGXComputeCommandEncoderDebugging::Get, (FAGXCommandEncoderDebugging const&)*this);
}
FAGXComputeCommandEncoderDebugging::FAGXComputeCommandEncoderDebugging(FAGXDebugCommandEncoder* handle)
: FAGXCommandEncoderDebugging((FAGXDebugCommandEncoder*)handle)
{
	
}

FAGXComputeCommandEncoderDebugging FAGXComputeCommandEncoderDebugging::Get(mtlpp::ComputeCommandEncoder& Encoder)
{
	return Encoder.GetAssociatedObject<FAGXComputeCommandEncoderDebugging>((void const*)&FAGXComputeCommandEncoderDebugging::Get);
}

void FAGXComputeCommandEncoderDebugging::InsertDebugSignpost(ns::String const& Label)
{
	((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.InsertDebugSignpost(Label);
}
void FAGXComputeCommandEncoderDebugging::PushDebugGroup(ns::String const& Group)
{
    ((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.PushDebugGroup(Group);
}
void FAGXComputeCommandEncoderDebugging::PopDebugGroup()
{
    ((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.PopDebugGroup();
#if METAL_DEBUG_OPTIONS
	InsertDebugDispatch();
#endif
}

void FAGXComputeCommandEncoderDebugging::EndEncoder()
{
	((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.EndCommandEncoder();
}

void FAGXComputeCommandEncoderDebugging::DispatchThreadgroups(mtlpp::Size const& threadgroupsPerGrid, mtlpp::Size const& threadsPerThreadgroup)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.Dispatch([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		case EAGXDebugLevelValidation:
		{
			Validate();
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::SetPipeline(FAGXShaderPipeline* Pipeline)
{
#if METAL_DEBUG_OPTIONS
	((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline = [Pipeline retain];
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.SetPipeline(Pipeline->ComputePipelineState.GetLabel());
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::SetBytes(const void * bytes, NSUInteger length, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Buffers[index] = nil;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Bytes[index] = bytes;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Offsets[index] = length;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask = bytes ? (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask | (1 << (index))) : (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask & ~(1 << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}
void FAGXComputeCommandEncoderDebugging::SetBuffer( FAGXBuffer const& buffer, NSUInteger offset, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Buffers[index] = buffer;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Bytes[index] = nil;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Offsets[index] = offset;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask = buffer ? (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask | (1 << (index))) : (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask & ~(1 << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}
void FAGXComputeCommandEncoderDebugging::SetBufferOffset(NSUInteger offset, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Offsets[index] = offset;
			check(((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.BufferMask & (1 << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::SetTexture( FAGXTexture const& texture, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderTextures.Textures[index] = texture;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.TextureMask = texture ? (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.TextureMask | (FAGXTextureMask(1) << (index))) : (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.TextureMask & ~(FAGXTextureMask(1) << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::SetSamplerState( mtlpp::SamplerState const& sampler, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderSamplers.Samplers[index] = sampler;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask = sampler ? (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask | (1 << (index))) : (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask & ~(1 << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::SetSamplerState( mtlpp::SamplerState const& sampler, float lodMinClamp, float lodMaxClamp, NSUInteger index)
{
#if METAL_DEBUG_OPTIONS
	switch (((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderSamplers.Samplers[index] = sampler;
			((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask = sampler ? (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask | (1 << (index))) : (((FAGXDebugComputeCommandEncoder*)m_ptr)->ResourceMask.SamplerMask & ~(1 << (index)));
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::DispatchThreadgroupsWithIndirectBuffer(FAGXBuffer const& indirectBuffer, NSUInteger indirectBufferOffset, mtlpp::Size const& threadsPerThreadgroup)
{
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		{
			((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.Dispatch([NSString stringWithFormat:@"%s", __PRETTY_FUNCTION__]);
		}
		case EAGXDebugLevelValidation:
		{
			Validate();
		}
		default:
		{
			break;
		}
	}
#endif
}

void FAGXComputeCommandEncoderDebugging::Validate()
{
	bool bOK = true;
	
#if METAL_DEBUG_OPTIONS
	switch(((FAGXDebugComputeCommandEncoder*)m_ptr)->Buffer.GetPtr()->DebugLevel)
	{
		case EAGXDebugLevelConditionalSubmit:
		case EAGXDebugLevelWaitForComplete:
		case EAGXDebugLevelLogOperations:
		case EAGXDebugLevelValidation:
		{
			check(((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline);
			
			MTLComputePipelineReflection* Reflection = ((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline->ComputePipelineReflection;
			check(Reflection);
	
			NSArray<MTLArgument*>* Arguments = Reflection.arguments;
			for (uint32 i = 0; i < Arguments.count; i++)
			{
				MTLArgument* Arg = [Arguments objectAtIndex:i];
				check(Arg);
				switch(Arg.type)
				{
					case MTLArgumentTypeBuffer:
					{
						checkf(Arg.index < ML_MaxBuffers, TEXT("Metal buffer index exceeded!"));
						if ((((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Buffers[Arg.index] == nil && ((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderBuffers.Bytes[Arg.index] == nil))
						{
							UE_LOG(LogAGX, Warning, TEXT("Unbound buffer at Metal index %u which will crash the driver: %s"), (uint32)Arg.index, *FString([Arg description]));
							bOK = false;
						}
						break;
					}
					case MTLArgumentTypeThreadgroupMemory:
					{
						break;
					}
					case MTLArgumentTypeTexture:
					{
						checkf(Arg.index < ML_MaxTextures, TEXT("Metal texture index exceeded!"));
						if (((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderTextures.Textures[Arg.index] == nil)
						{
							UE_LOG(LogAGX, Warning, TEXT("Unbound texture at Metal index %u  which will crash the driver: %s"), (uint32)Arg.index, *FString([Arg description]));
							bOK = false;
						}
						else if (((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderTextures.Textures[Arg.index].textureType != Arg.textureType)
						{
							UE_LOG(LogAGX, Warning, TEXT("Incorrect texture type bound at Metal index %u which will crash the driver: %s\n%s"), (uint32)Arg.index, *FString([Arg description]), *FString([((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderTextures.Textures[Arg.index] description]));
							bOK = false;
						}
						break;
					}
					case MTLArgumentTypeSampler:
					{
						checkf(Arg.index < ML_MaxSamplers, TEXT("Metal sampler index exceeded!"));
						if (((FAGXDebugComputeCommandEncoder*)m_ptr)->ShaderSamplers.Samplers[Arg.index] == nil)
						{
							UE_LOG(LogAGX, Warning, TEXT("Unbound sampler at Metal index %u which will crash the driver: %s"), (uint32)Arg.index, *FString([Arg description]));
							bOK = false;
						}
						break;
					}
					default:
						check(false);
						break;
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}
	
    if (!bOK)
    {
        UE_LOG(LogAGX, Error, TEXT("Metal Validation failures for compute shader:\n%s"), (((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline && ((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline->ComputeSource) ? *FString(((FAGXDebugComputeCommandEncoder*)m_ptr)->Pipeline->ComputeSource) : TEXT("nil"));
    }
#endif
}

NS_ASSUME_NONNULL_END
#endif
