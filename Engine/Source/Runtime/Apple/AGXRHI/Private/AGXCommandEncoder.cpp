// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandEncoder.cpp: AGX RHI command encoder wrapper.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXCommandBufferFence.h"
#include "AGXCommandEncoder.h"
#include "AGXProfiler.h"
#include "MetalShaderResources.h"

const uint32 EncoderRingBufferSize = 1024 * 1024;

#if METAL_DEBUG_OPTIONS
extern int32 GAGXBufferScribble;
#endif

#pragma mark - Public C++ Boilerplate -

FAGXCommandEncoder::FAGXCommandEncoder(FAGXCommandList& CmdList, EAGXCommandEncoderType InType)
: CommandList(CmdList)
, bSupportsMetalFeaturesSetBytes(CmdList.GetCommandQueue().SupportsFeature(EAGXFeaturesSetBytes))
, RingBuffer(EncoderRingBufferSize, BufferOffsetAlignment, (MTLResourceCPUCacheModeDefaultCache | BUFFER_RESOURCE_STORAGE_MANAGED | MTLResourceHazardTrackingModeDefault))
, RenderPassDesc(nil)
, ParallelRenderCommandEncoder(nil)
, RenderCommandEncoder(nil)
, ComputeCommandEncoder(nil)
, BlitCommandEncoder(nil)
#if ENABLE_METAL_GPUPROFILE
, CommandBufferStats(nullptr)
#endif
, DebugGroups([NSMutableArray new])
, EncoderNum(0)
, CmdBufIndex(0)
, Type(InType)
{
	for (uint32 Frequency = 0; Frequency < uint32(mtlpp::FunctionType::Kernel)+1; Frequency++)
	{
		FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		FMemory::Memzero(ShaderBuffers[Frequency].Usage);
		ShaderBuffers[Frequency].SideTable = [[FAGXBufferData alloc] init];
		ShaderBuffers[Frequency].SideTable->Data = (uint8*)(&ShaderBuffers[Frequency].Lengths[0]);
		ShaderBuffers[Frequency].SideTable->Len = sizeof(ShaderBuffers[Frequency].Lengths);
		ShaderBuffers[Frequency].Bound = 0;
	}
	
	for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
	{
		ColorStoreActions[i] = MTLStoreActionUnknown;
	}
	DepthStoreAction = MTLStoreActionUnknown;
	StencilStoreAction = MTLStoreActionUnknown;
}

FAGXCommandEncoder::~FAGXCommandEncoder(void)
{
	if(CommandBuffer)
	{
		EndEncoding();
		CommitCommandBuffer(false);
	}
	
	check(!IsRenderCommandEncoderActive());
	check(!IsComputeCommandEncoderActive());
	check(!IsBlitCommandEncoderActive());
	
	AGXSafeReleaseMetalRenderPassDescriptor(RenderPassDesc);
	RenderPassDesc = nil;

	if(DebugGroups)
	{
		[DebugGroups release];
	}
	
	for (uint32 Frequency = 0; Frequency < uint32(mtlpp::FunctionType::Kernel)+1; Frequency++)
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ShaderBuffers[Frequency].Buffers[i] = nil;
		}
		FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		FMemory::Memzero(ShaderBuffers[Frequency].Usage);
		ShaderBuffers[Frequency].SideTable->Data = nullptr;
		[ShaderBuffers[Frequency].SideTable release];
		ShaderBuffers[Frequency].SideTable = nil;
		ShaderBuffers[Frequency].Bound = 0;
	}
}

void FAGXCommandEncoder::Reset(void)
{
    check(IsRenderCommandEncoderActive() == false
          && IsComputeCommandEncoderActive() == false
          && IsBlitCommandEncoderActive() == false);
	
	if(RenderPassDesc)
	{
		AGXSafeReleaseMetalRenderPassDescriptor(RenderPassDesc);
		RenderPassDesc = nil;
	}
	
	{
		for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
		{
			ColorStoreActions[i] = MTLStoreActionUnknown;
		}
		DepthStoreAction = MTLStoreActionUnknown;
		StencilStoreAction = MTLStoreActionUnknown;
	}
	
	for (uint32 Frequency = 0; Frequency < uint32(mtlpp::FunctionType::Kernel)+1; Frequency++)
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ShaderBuffers[Frequency].Buffers[i] = nil;
		}
    	FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		FMemory::Memzero(ShaderBuffers[Frequency].Usage);
		ShaderBuffers[Frequency].Bound = 0;
	}
	
	[DebugGroups removeAllObjects];
}

void FAGXCommandEncoder::ResetLive(void)
{
	for (uint32 Frequency = 0; Frequency < uint32(mtlpp::FunctionType::Kernel)+1; Frequency++)
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ShaderBuffers[Frequency].Buffers[i] = nil;
		}
		FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		ShaderBuffers[Frequency].Bound = 0;
	}
	
	if (IsRenderCommandEncoderActive())
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			[RenderCommandEncoder setVertexBuffer:nil offset:0 atIndex:i];
			[RenderCommandEncoder setFragmentBuffer:nil offset:0 atIndex:i];
		}
		
		for (uint32 i = 0; i < ML_MaxTextures; i++)
		{
			[RenderCommandEncoder setVertexTexture:nil atIndex:i];
			[RenderCommandEncoder setFragmentTexture:nil atIndex:i];
		}
	}
	else if (IsComputeCommandEncoderActive())
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			[ComputeCommandEncoder setBuffer:nil offset:0 atIndex:i];
		}
		
		for (uint32 i = 0; i < ML_MaxTextures; i++)
		{
			[ComputeCommandEncoder setTexture:nil atIndex:i];
		}
	}
}

#pragma mark - Public Command Buffer Mutators -

void FAGXCommandEncoder::StartCommandBuffer(void)
{
	check(!CommandBuffer || EncoderNum == 0);
	check(IsRenderCommandEncoderActive() == false
          && IsComputeCommandEncoderActive() == false
          && IsBlitCommandEncoderActive() == false);

	if (!CommandBuffer)
	{
		CmdBufIndex++;
		CommandBuffer = CommandList.GetCommandQueue().CreateCommandBuffer();
		
		if ([DebugGroups count] > 0)
		{
			CommandBuffer.SetLabel([DebugGroups lastObject]);
		}
		
	#if ENABLE_METAL_GPUPROFILE
		FAGXProfiler* Profiler = FAGXProfiler::GetProfiler();
		if (Profiler)
		{
			CommandBufferStats = Profiler->AllocateCommandBuffer(CommandBuffer, 0);
		}
	#endif
	}
}
	
void FAGXCommandEncoder::CommitCommandBuffer(uint32 const Flags)
{
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false
          && IsComputeCommandEncoderActive() == false
          && IsBlitCommandEncoderActive() == false);

	bool const bWait = (Flags & EAGXSubmitFlagsWaitOnCommandBuffer);
	bool const bIsLastCommandBuffer = (Flags & EAGXSubmitFlagsLastCommandBuffer);
	
	if (EncoderNum == 0 && !bWait && !(Flags & EAGXSubmitFlagsForce))
	{
		return;
	}
	
	if(CommandBuffer.GetLabel() == nil && [DebugGroups count] > 0)
	{
		CommandBuffer.SetLabel([DebugGroups lastObject]);
	}
	
	if (!(Flags & EAGXSubmitFlagsBreakCommandBuffer))
	{
		RingBuffer.Commit(CommandBuffer);
	}
	else
	{
		RingBuffer.Submit();
	}
    
#if METAL_DEBUG_OPTIONS
    if(CommandList.GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation)
    {
        for (FAGXBuffer const& Buffer : ActiveBuffers)
        {
            GetAGXDeviceContext().AddActiveBuffer(Buffer);
        }
        
        TSet<ns::AutoReleased<FAGXBuffer>> NewActiveBuffers = MoveTemp(ActiveBuffers);
        AddCompletionHandler([NewActiveBuffers](mtlpp::CommandBuffer const&)
                             {
                                 for (FAGXBuffer const& Buffer : NewActiveBuffers)
                                 {
                                     GetAGXDeviceContext().RemoveActiveBuffer(Buffer);
                                 }
                             });
    }
#endif
#if ENABLE_METAL_GPUPROFILE
	CommandBufferStats->End(CommandBuffer);
	CommandBufferStats = nullptr;
#endif

	CommandList.Commit(CommandBuffer, MoveTemp(CompletionHandlers), bWait, bIsLastCommandBuffer);
	
	CommandBuffer = nil;
	if (Flags & EAGXSubmitFlagsCreateCommandBuffer)
	{
		StartCommandBuffer();
		check(CommandBuffer);
	}
	
	BufferBindingHistory.Empty();
	TextureBindingHistory.Empty();
	
	EncoderNum = 0;
}

#pragma mark - Public Command Encoder Accessors -
	
bool FAGXCommandEncoder::IsParallelRenderCommandEncoderActive() const
{
	return (ParallelRenderCommandEncoder != nil);
}
	
bool FAGXCommandEncoder::IsRenderCommandEncoderActive() const
{
	return (RenderCommandEncoder != nil) || (ParallelRenderCommandEncoder != nil);
}

bool FAGXCommandEncoder::IsComputeCommandEncoderActive() const
{
	return (ComputeCommandEncoder != nil);
}

bool FAGXCommandEncoder::IsBlitCommandEncoderActive() const
{
	return (BlitCommandEncoder != nil);
}

bool FAGXCommandEncoder::IsImmediate(void) const
{
	return CommandList.IsImmediate();
}

bool FAGXCommandEncoder::IsParallel(void) const
{
	return CommandList.IsParallel() && (Type == EAGXCommandEncoderCurrent);
}

bool FAGXCommandEncoder::IsRenderPassDescriptorValid() const
{
	return (RenderPassDesc != nil);
}

MTLRenderPassDescriptor const* FAGXCommandEncoder::GetRenderPassDescriptor() const
{
	return RenderPassDesc;
}

id<MTLParallelRenderCommandEncoder> FAGXCommandEncoder::GetParallelRenderCommandEncoder() const
{
	return ParallelRenderCommandEncoder;
}

id<MTLRenderCommandEncoder> FAGXCommandEncoder::GetChildRenderCommandEncoder(uint32 Index) const
{
	check(IsParallelRenderCommandEncoderActive() && Index < ChildRenderCommandEncoders.Num());
	return ChildRenderCommandEncoders[Index];
}

id<MTLRenderCommandEncoder> FAGXCommandEncoder::GetRenderCommandEncoder() const
{
	check(IsRenderCommandEncoderActive() && RenderCommandEncoder);
	return RenderCommandEncoder;
}

id<MTLComputeCommandEncoder> FAGXCommandEncoder::GetComputeCommandEncoder() const
{
	check(IsComputeCommandEncoderActive());
	return ComputeCommandEncoder;
}

id<MTLBlitCommandEncoder> FAGXCommandEncoder::GetBlitCommandEncoder() const
{
	check(IsBlitCommandEncoderActive());
	return BlitCommandEncoder;
}

#pragma mark - Public Command Encoder Mutators -

void FAGXCommandEncoder::BeginParallelRenderCommandEncoding(uint32 NumChildren)
{
	check(IsImmediate());
	check(RenderPassDesc);
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	ParallelRenderCommandEncoder = [[CommandBuffer.GetPtr() parallelRenderCommandEncoderWithDescriptor:RenderPassDesc] retain];
	
	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"ParallelRenderCommandEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		[ParallelRenderCommandEncoder setLabel:Label];
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				[ParallelRenderCommandEncoder pushDebugGroup:Group];
			}
		}
	}
	
	for (uint32 i = 0; i < NumChildren; i++)
	{
		id<MTLRenderCommandEncoder> CommandEncoder = [[ParallelRenderCommandEncoder renderCommandEncoder] retain];
		ChildRenderCommandEncoders.Add(CommandEncoder);
	}
}

void FAGXCommandEncoder::BeginRenderCommandEncoding(void)
{
	check(RenderPassDesc);
	check(CommandList.IsParallel() || CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	if (!CommandList.IsParallel() || Type == EAGXCommandEncoderPrologue)
	{
		RenderCommandEncoder = [[CommandBuffer.GetPtr() renderCommandEncoderWithDescriptor:RenderPassDesc] retain];
		EncoderNum++;	
	}
	else
	{
		RenderCommandEncoder = GetAGXDeviceContext().GetParallelRenderCommandEncoder(CommandList.GetParallelIndex(), &ParallelRenderCommandEncoder, CommandBuffer);
	}
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"RenderEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		[RenderCommandEncoder setLabel:Label];
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				[RenderCommandEncoder pushDebugGroup:Group];
			}
		}
	}
}

void FAGXCommandEncoder::BeginComputeCommandEncoding(MTLDispatchType DispatchType)
{
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	if (DispatchType == MTLDispatchTypeSerial)
	{
		ComputeCommandEncoder = [[CommandBuffer.GetPtr() computeCommandEncoder] retain];
	}
	else
	{
		ComputeCommandEncoder = [[CommandBuffer.GetPtr() computeCommandEncoderWithDispatchType:DispatchType] retain];
	}

	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"ComputeEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		[ComputeCommandEncoder setLabel:Label];
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				[ComputeCommandEncoder pushDebugGroup:Group];
			}
		}
	}
}

void FAGXCommandEncoder::BeginBlitCommandEncoding(void)
{
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	BlitCommandEncoder = [[CommandBuffer.GetPtr() blitCommandEncoder] retain];
	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"BlitEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		[BlitCommandEncoder setLabel:Label];
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				[BlitCommandEncoder pushDebugGroup:Group];
			}
		}
	}
}

void FAGXCommandEncoder::EndEncoding(void)
{
	@autoreleasepool
	{
		if(IsRenderCommandEncoderActive())
		{
			if (RenderCommandEncoder)
			{
				if (ParallelRenderCommandEncoder == nil)
				{
					check(RenderPassDesc);
					
					MTLRenderPassColorAttachmentDescriptorArray* ColorAttachments = [RenderPassDesc colorAttachments];
					for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
					{
						MTLRenderPassColorAttachmentDescriptor* ColorAttachment = [ColorAttachments objectAtIndexedSubscript:i];
						if ([ColorAttachment texture] && ([ColorAttachment storeAction] == MTLStoreActionUnknown))
						{
							MTLStoreAction Action = ColorStoreActions[i];
							check(Action != MTLStoreActionUnknown);
							[RenderCommandEncoder setColorStoreAction:Action atIndex:i];
						}
					}

					MTLRenderPassDepthAttachmentDescriptor* DepthAttachment = [RenderPassDesc depthAttachment];
					if ([DepthAttachment texture] && ([DepthAttachment storeAction] == MTLStoreActionUnknown))
					{
						MTLStoreAction Action = DepthStoreAction;
						check(Action != MTLStoreActionUnknown);
						[RenderCommandEncoder setDepthStoreAction:Action];
					}

					MTLRenderPassStencilAttachmentDescriptor* StencilAttachment = [RenderPassDesc stencilAttachment];
					if ([StencilAttachment texture] && ([StencilAttachment storeAction] == MTLStoreActionUnknown))
					{
						MTLStoreAction Action = StencilStoreAction;
						check(Action != MTLStoreActionUnknown);
						[RenderCommandEncoder setStencilStoreAction:Action];
					}
				}
				
				[RenderCommandEncoder endEncoding];
				[RenderCommandEncoder release];
				RenderCommandEncoder = nil;
			}
			
			if (ParallelRenderCommandEncoder && IsParallel())
			{
				RingBuffer.Commit(CommandBuffer);
				
#if METAL_DEBUG_OPTIONS
				if(CommandList.GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation)
				{
					for (FAGXBuffer const& Buffer : ActiveBuffers)
					{
						GetAGXDeviceContext().AddActiveBuffer(Buffer);
					}
					
					TSet<ns::AutoReleased<FAGXBuffer>> NewActiveBuffers = MoveTemp(ActiveBuffers);
					AddCompletionHandler([NewActiveBuffers](mtlpp::CommandBuffer const&)
										{
											for (FAGXBuffer const& Buffer : NewActiveBuffers)
											{
												GetAGXDeviceContext().RemoveActiveBuffer(Buffer);
											}
										});
				}
#endif // METAL_DEBUG_OPTIONS

				BufferBindingHistory.Empty();
				TextureBindingHistory.Empty();
				
				EncoderNum = 0;

				CommandBuffer = nil;

				[ParallelRenderCommandEncoder release];
				ParallelRenderCommandEncoder = nil;
			}

			if (ParallelRenderCommandEncoder && IsImmediate())
			{
				{
					check(RenderPassDesc);
					
					MTLRenderPassColorAttachmentDescriptorArray* ColorAttachments = [RenderPassDesc colorAttachments];
					for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
					{
						MTLRenderPassColorAttachmentDescriptor* ColorAttachment = [ColorAttachments objectAtIndexedSubscript:i];
						if ([ColorAttachment texture] && ([ColorAttachment storeAction] == MTLStoreActionUnknown))
						{
							MTLStoreAction Action = ColorStoreActions[i];
							check(Action != MTLStoreActionUnknown);
							[ParallelRenderCommandEncoder setColorStoreAction:Action atIndex:i];
						}
					}

					MTLRenderPassDepthAttachmentDescriptor* DepthAttachment = [RenderPassDesc depthAttachment];
					if ([DepthAttachment texture] && ([DepthAttachment storeAction] == MTLStoreActionUnknown))
					{
						MTLStoreAction Action = DepthStoreAction;
						check(Action != MTLStoreActionUnknown);
						[ParallelRenderCommandEncoder setDepthStoreAction:Action];
					}

					MTLRenderPassStencilAttachmentDescriptor* StencilAttachment = [RenderPassDesc stencilAttachment];
					if ([StencilAttachment texture] && ([StencilAttachment storeAction] == MTLStoreActionUnknown))
					{
						MTLStoreAction Action = StencilStoreAction;
						check(Action != MTLStoreActionUnknown);
						[ParallelRenderCommandEncoder setStencilStoreAction:Action];
					}
				}

				[ParallelRenderCommandEncoder endEncoding];
				[ParallelRenderCommandEncoder release];
				ParallelRenderCommandEncoder = nil;

				ChildRenderCommandEncoders.Empty();
			}
		}
		else if(IsComputeCommandEncoderActive())
		{
			[ComputeCommandEncoder endEncoding];
			[ComputeCommandEncoder release];
			ComputeCommandEncoder = nil;
		}
		else if(IsBlitCommandEncoderActive())
		{
			[BlitCommandEncoder endEncoding];
			[BlitCommandEncoder release];
			BlitCommandEncoder = nil;
		}
	}
	
	for (uint32 Frequency = 0; Frequency < uint32(mtlpp::FunctionType::Kernel)+1; Frequency++)
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ShaderBuffers[Frequency].Buffers[i] = nil;
		}
		FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		FMemory::Memzero(ShaderBuffers[Frequency].Usage);
		ShaderBuffers[Frequency].Bound = 0;
	}
}

void FAGXCommandEncoder::InsertCommandBufferFence(FAGXCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler)
{
	check(CommandBuffer);
	
	Fence.CommandBufferFence = CommandBuffer.GetCompletionFence();
	
	if (Handler)
	{
		AddCompletionHandler(Handler);
	}
}

void FAGXCommandEncoder::AddCompletionHandler(mtlpp::CommandBufferHandler Handler)
{
	check(Handler);
	
	mtlpp::CommandBufferHandler HeapHandler = Block_copy(Handler);
	CompletionHandlers.Add(HeapHandler);
	Block_release(HeapHandler);
}

#pragma mark - Public Debug Support -

void FAGXCommandEncoder::InsertDebugSignpost(ns::String const& String)
{
	if (String)
	{
		if (RenderCommandEncoder)
		{
			[RenderCommandEncoder insertDebugSignpost:String.GetPtr()];
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			[ParallelRenderCommandEncoder insertDebugSignpost:String.GetPtr()];
		}
		else if (ComputeCommandEncoder)
		{
			[ComputeCommandEncoder insertDebugSignpost:String.GetPtr()];
		}
		else if (BlitCommandEncoder)
		{
			[BlitCommandEncoder insertDebugSignpost:String.GetPtr()];
		}
	}
}

void FAGXCommandEncoder::PushDebugGroup(ns::String const& String)
{
	if (String)
	{
		[DebugGroups addObject:String.GetPtr()];
		if (RenderCommandEncoder)
		{
			[RenderCommandEncoder pushDebugGroup:String.GetPtr()];
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			[ParallelRenderCommandEncoder pushDebugGroup:String.GetPtr()];
		}
		else if (ComputeCommandEncoder)
		{
			[ComputeCommandEncoder pushDebugGroup:String.GetPtr()];
		}
		else if (BlitCommandEncoder)
		{
			[BlitCommandEncoder pushDebugGroup:String.GetPtr()];
		}
	}
}

void FAGXCommandEncoder::PopDebugGroup(void)
{
	if (DebugGroups.count > 0)
	{
		[DebugGroups removeLastObject];
		if (RenderCommandEncoder)
		{
			[RenderCommandEncoder popDebugGroup];
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			[ParallelRenderCommandEncoder popDebugGroup];
		}
		else if (ComputeCommandEncoder)
		{
			[ComputeCommandEncoder popDebugGroup];
		}
		else if (BlitCommandEncoder)
		{
			[BlitCommandEncoder popDebugGroup];
		}
	}
}

#if ENABLE_METAL_GPUPROFILE
FAGXCommandBufferStats* FAGXCommandEncoder::GetCommandBufferStats(void)
{
	return CommandBufferStats;
}
#endif

#pragma mark - Public Render State Mutators -

void FAGXCommandEncoder::SetRenderPassDescriptor(MTLRenderPassDescriptor* InRenderPassDesc)
{
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	check(InRenderPassDesc);
	
	if (InRenderPassDesc != RenderPassDesc)
	{
		AGXSafeReleaseMetalRenderPassDescriptor(RenderPassDesc);
		RenderPassDesc = InRenderPassDesc;
		{
			for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
			{
				ColorStoreActions[i] = MTLStoreActionUnknown;
			}
			DepthStoreAction = MTLStoreActionUnknown;
			StencilStoreAction = MTLStoreActionUnknown;
		}
	}
	check(RenderPassDesc);
	
	for (uint32 Frequency = 0; Frequency < uint32(MTLFunctionTypeKernel)+1; Frequency++)
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ShaderBuffers[Frequency].Buffers[i] = nil;
		}
		FMemory::Memzero(ShaderBuffers[Frequency].Bytes);
		FMemory::Memzero(ShaderBuffers[Frequency].Offsets);
		FMemory::Memzero(ShaderBuffers[Frequency].Lengths);
		FMemory::Memzero(ShaderBuffers[Frequency].Usage);
		ShaderBuffers[Frequency].Bound = 0;
	}
}

void FAGXCommandEncoder::SetRenderPassStoreActions(MTLStoreAction const* ColorStore, MTLStoreAction DepthStore, MTLStoreAction StencilStore)
{
	check(RenderPassDesc);
	{
		for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
		{
			ColorStoreActions[i] = ColorStore[i];
		}
		DepthStoreAction = DepthStore;
		StencilStoreAction = StencilStore;
	}
}

void FAGXCommandEncoder::SetRenderPipelineState(FAGXShaderPipeline* PipelineState)
{
	check(RenderCommandEncoder);

	[RenderCommandEncoder setRenderPipelineState:PipelineState->RenderPipelineState];
}

void FAGXCommandEncoder::SetViewport(MTLViewport const Viewport[], uint32 NumActive)
{
	check(RenderCommandEncoder);
	check(NumActive >= 1 && NumActive < ML_MaxViewports);
	if (NumActive == 1)
	{
		[RenderCommandEncoder setViewport:Viewport[0]];
	}
#if PLATFORM_MAC
	else
	{
		check(FAGXCommandQueue::SupportsFeature(EAGXFeaturesMultipleViewports));
		[RenderCommandEncoder setViewports:Viewport count:NumActive];
	}
#endif
}

void FAGXCommandEncoder::SetFrontFacingWinding(MTLWinding InFrontFacingWinding)
{
    check (RenderCommandEncoder);
	{
		[RenderCommandEncoder setFrontFacingWinding:InFrontFacingWinding];
	}
}

void FAGXCommandEncoder::SetCullMode(MTLCullMode InCullMode)
{
    check (RenderCommandEncoder);
	{
		[RenderCommandEncoder setCullMode:InCullMode];
	}
}

void FAGXCommandEncoder::SetDepthBias(float const InDepthBias, float const InSlopeScale, float const InClamp)
{
    check (RenderCommandEncoder);
	{
		[RenderCommandEncoder setDepthBias:InDepthBias slopeScale:InSlopeScale clamp:InClamp];
	}
}

void FAGXCommandEncoder::SetScissorRect(MTLScissorRect const Rect[], uint32 NumActive)
{
    check(RenderCommandEncoder);
	check(NumActive >= 1 && NumActive < ML_MaxViewports);
	if (NumActive == 1)
	{
		[RenderCommandEncoder setScissorRect:Rect[0]];
	}
#if PLATFORM_MAC
	else
	{
		check(FAGXCommandQueue::SupportsFeature(EAGXFeaturesMultipleViewports));
		[RenderCommandEncoder setScissorRects:Rect count:NumActive];
	}
#endif
}

void FAGXCommandEncoder::SetTriangleFillMode(mtlpp::TriangleFillMode const InFillMode)
{
    check(RenderCommandEncoder);
	{
		[RenderCommandEncoder setTriangleFillMode:(MTLTriangleFillMode)InFillMode];
	}
}

void FAGXCommandEncoder::SetDepthClipMode(mtlpp::DepthClipMode const InDepthClipMode)
{
	check(RenderCommandEncoder);
	{
		[RenderCommandEncoder setDepthClipMode:(MTLDepthClipMode)InDepthClipMode];
	}
}

void FAGXCommandEncoder::SetBlendColor(float const Red, float const Green, float const Blue, float const Alpha)
{
	check(RenderCommandEncoder);
	{
		[RenderCommandEncoder setBlendColorRed:Red green:Green blue:Blue alpha:Alpha];
	}
}

void FAGXCommandEncoder::SetDepthStencilState(id<MTLDepthStencilState> InDepthStencilState)
{
    check (RenderCommandEncoder);
	{
		[RenderCommandEncoder setDepthStencilState:InDepthStencilState];
	}
}

void FAGXCommandEncoder::SetStencilReferenceValue(uint32 const ReferenceValue)
{
    check (RenderCommandEncoder);
	{
		[RenderCommandEncoder setStencilReferenceValue:ReferenceValue];
	}
}

void FAGXCommandEncoder::SetVisibilityResultMode(mtlpp::VisibilityResultMode const Mode, NSUInteger const Offset)
{
    check (RenderCommandEncoder);
	{
		check(Mode == mtlpp::VisibilityResultMode::Disabled || [RenderPassDesc visibilityResultBuffer]);
		[RenderCommandEncoder setVisibilityResultMode:(MTLVisibilityResultMode)Mode offset:Offset];
	}
}
	
#pragma mark - Public Shader Resource Mutators -

void FAGXCommandEncoder::SetShaderBuffer(mtlpp::FunctionType FunctionType, FAGXBuffer const& Buffer, NSUInteger Offset, NSUInteger Length, NSUInteger index, MTLResourceUsage Usage, EPixelFormat Format, NSUInteger ElementRowPitch)
{
	check(index < ML_MaxBuffers);
    if(GetAGXDeviceContext().SupportsFeature(EAGXFeaturesSetBufferOffset) && Buffer && (ShaderBuffers[uint32(FunctionType)].Bound & (1 << index)) && ShaderBuffers[uint32(FunctionType)].Buffers[index] == Buffer)
    {
		if (FunctionType == mtlpp::FunctionType::Vertex || FunctionType == mtlpp::FunctionType::Kernel)
		{
			FenceResource(Buffer);
		}
		SetShaderBufferOffset(FunctionType, Offset, Length, index);
		ShaderBuffers[uint32(FunctionType)].Usage[index] = Usage;
		ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(index, Length, GAGXBufferFormats[Format].DataFormat, ElementRowPitch);
	}
    else
    {
		if(Buffer)
		{
			ShaderBuffers[uint32(FunctionType)].Bound |= (1 << index);
		}
		else
		{
			ShaderBuffers[uint32(FunctionType)].Bound &= ~(1 << index);
		}
		ShaderBuffers[uint32(FunctionType)].Buffers[index] = Buffer;
		ShaderBuffers[uint32(FunctionType)].Bytes[index] = nil;
		ShaderBuffers[uint32(FunctionType)].Offsets[index] = Offset;
		ShaderBuffers[uint32(FunctionType)].Usage[index] = Usage;
		ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(index, Length, GAGXBufferFormats[Format].DataFormat, ElementRowPitch);
		
		SetShaderBufferInternal(FunctionType, index);
    }
    
	if (Buffer)
	{
		BufferBindingHistory.Add(ns::AutoReleased<FAGXBuffer>(Buffer));
	}
}

void FAGXCommandEncoder::SetShaderData(mtlpp::FunctionType const FunctionType, FAGXBufferData* Data, NSUInteger const Offset, NSUInteger const Index, EPixelFormat const Format, NSUInteger const ElementRowPitch)
{
	check(Index < ML_MaxBuffers);
	
	if(Data)
	{
		ShaderBuffers[uint32(FunctionType)].Bound |= (1 << Index);
	}
	else
	{
		ShaderBuffers[uint32(FunctionType)].Bound &= ~(1 << Index);
	}
	
	ShaderBuffers[uint32(FunctionType)].Buffers[Index] = nil;
	ShaderBuffers[uint32(FunctionType)].Bytes[Index] = Data;
	ShaderBuffers[uint32(FunctionType)].Offsets[Index] = Offset;
	ShaderBuffers[uint32(FunctionType)].Usage[Index] = MTLResourceUsageRead;
	ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(Index, Data ? (Data->Len - Offset) : 0, GAGXBufferFormats[Format].DataFormat, ElementRowPitch);
	
	SetShaderBufferInternal(FunctionType, Index);
}

void FAGXCommandEncoder::SetShaderBytes(mtlpp::FunctionType const FunctionType, uint8 const* Bytes, NSUInteger const Length, NSUInteger const Index)
{
	check(Index < ML_MaxBuffers);
	
	if(Bytes && Length)
	{
		ShaderBuffers[uint32(FunctionType)].Bound |= (1 << Index);

		if (bSupportsMetalFeaturesSetBytes)
		{
			switch (FunctionType)
			{
				case mtlpp::FunctionType::Vertex:
					check(RenderCommandEncoder);
					[RenderCommandEncoder setVertexBytes:static_cast<const void*>(Bytes) length:Length atIndex:Index];
					break;
				case mtlpp::FunctionType::Fragment:
					check(RenderCommandEncoder);
					[RenderCommandEncoder setFragmentBytes:static_cast<const void*>(Bytes) length:Length atIndex:Index];
					break;
				case mtlpp::FunctionType::Kernel:
					check(ComputeCommandEncoder);
					[ComputeCommandEncoder setBytes:static_cast<const void*>(Bytes) length:Length atIndex:Index];
					break;
				default:
					check(false);
					break;
			}
			
			ShaderBuffers[uint32(FunctionType)].Buffers[Index] = nil;
		}
		else
		{
			FAGXBuffer Buffer = RingBuffer.NewBuffer(Length, BufferOffsetAlignment);
			FMemory::Memcpy(((uint8*)Buffer.GetContents()), Bytes, Length);
			ShaderBuffers[uint32(FunctionType)].Buffers[Index] = Buffer;
		}
		ShaderBuffers[uint32(FunctionType)].Bytes[Index] = nil;
		ShaderBuffers[uint32(FunctionType)].Offsets[Index] = 0;
		ShaderBuffers[uint32(FunctionType)].Usage[Index] = MTLResourceUsageRead;
		ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(Index, Length, GAGXBufferFormats[PF_Unknown].DataFormat, 0);
	}
	else
	{
		ShaderBuffers[uint32(FunctionType)].Bound &= ~(1 << Index);
		
		ShaderBuffers[uint32(FunctionType)].Buffers[Index] = nil;
		ShaderBuffers[uint32(FunctionType)].Bytes[Index] = nil;
		ShaderBuffers[uint32(FunctionType)].Offsets[Index] = 0;
		ShaderBuffers[uint32(FunctionType)].Usage[Index] = 0;
		ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(Index, 0, GAGXBufferFormats[PF_Unknown].DataFormat, 0);
	}
	
	SetShaderBufferInternal(FunctionType, Index);
}

void FAGXCommandEncoder::SetShaderBufferOffset(mtlpp::FunctionType FunctionType, NSUInteger const Offset, NSUInteger const Length, NSUInteger const index)
{
	check(index < ML_MaxBuffers);
    checkf(ShaderBuffers[uint32(FunctionType)].Buffers[index] && (ShaderBuffers[uint32(FunctionType)].Bound & (1 << index)), TEXT("Buffer must already be bound"));
	check(GetAGXDeviceContext().SupportsFeature(EAGXFeaturesSetBufferOffset));
	ShaderBuffers[uint32(FunctionType)].Offsets[index] = Offset;
	ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(index, Length, GAGXBufferFormats[PF_Unknown].DataFormat, 0);
	switch (FunctionType)
	{
		case mtlpp::FunctionType::Vertex:
			check (RenderCommandEncoder);
			[RenderCommandEncoder setVertexBufferOffset:(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset()) atIndex:index];
			break;
		case mtlpp::FunctionType::Fragment:
			check(RenderCommandEncoder);
			[RenderCommandEncoder setFragmentBufferOffset:(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset()) atIndex:index];
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			[ComputeCommandEncoder setBufferOffset:(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset()) atIndex:index];
			break;
		default:
			check(false);
			break;
	}
}

void FAGXCommandEncoder::SetShaderTexture(mtlpp::FunctionType FunctionType, FAGXTexture const& Texture, NSUInteger index, MTLResourceUsage Usage)
{
	check(index < ML_MaxTextures);
	switch (FunctionType)
	{
		case mtlpp::FunctionType::Vertex:
			check (RenderCommandEncoder);
			FenceResource(Texture);
			[RenderCommandEncoder setVertexTexture:Texture.GetPtr() atIndex:index];
			break;
		case mtlpp::FunctionType::Fragment:
			check(RenderCommandEncoder);
			[RenderCommandEncoder setFragmentTexture:Texture.GetPtr() atIndex:index];
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			FenceResource(Texture);
			[ComputeCommandEncoder setTexture:Texture.GetPtr() atIndex:index];
			break;
		default:
			check(false);
			break;
	}
	
	if (Texture)
	{
		uint8 Swizzle[4] = {0,0,0,0};
		assert(sizeof(Swizzle) == sizeof(uint32));
		if ((MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatX32_Stencil8
#if PLATFORM_MAC
		 ||	(MTLPixelFormat)Texture.GetPixelFormat() == MTLPixelFormatX24_Stencil8
#endif
		)
		{
			Swizzle[0] = Swizzle[1] = Swizzle[2] = Swizzle[3] = 1;
		}
		ShaderBuffers[uint32(FunctionType)].SetTextureSwizzle(index, Swizzle);
		TextureBindingHistory.Add(ns::AutoReleased<FAGXTexture>(Texture));
	}
}

void FAGXCommandEncoder::SetShaderSamplerState(mtlpp::FunctionType FunctionType, id<MTLSamplerState> Sampler, NSUInteger index)
{
	check(index < ML_MaxSamplers);
	switch (FunctionType)
	{
		case mtlpp::FunctionType::Vertex:
       		check (RenderCommandEncoder);
			[RenderCommandEncoder setVertexSamplerState:Sampler atIndex:index];
			break;
		case mtlpp::FunctionType::Fragment:
			check (RenderCommandEncoder);
			[RenderCommandEncoder setFragmentSamplerState:Sampler atIndex:index];
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			[ComputeCommandEncoder setSamplerState:Sampler atIndex:index];
			break;
		default:
			check(false);
			break;
	}
}

void FAGXCommandEncoder::SetShaderSideTable(mtlpp::FunctionType const FunctionType, NSUInteger const Index)
{
	if (Index < ML_MaxBuffers)
	{
		SetShaderData(FunctionType, ShaderBuffers[uint32(FunctionType)].SideTable, 0, Index);
	}
}

void FAGXCommandEncoder::TransitionResources(mtlpp::Resource const& Resource)
{
}

#pragma mark - Public Compute State Mutators -

void FAGXCommandEncoder::SetComputePipelineState(FAGXShaderPipeline* State)
{
	check (ComputeCommandEncoder);
	{
		[ComputeCommandEncoder setComputePipelineState:State->ComputePipelineState];
	}
}

#pragma mark - Public Ring-Buffer Accessor -
	
FAGXSubBufferRing& FAGXCommandEncoder::GetRingBuffer(void)
{
	return RingBuffer;
}

#pragma mark - Public Resource query Access -

bool FAGXCommandEncoder::HasTextureBindingHistory(FAGXTexture const& Texture) const
{
	return TextureBindingHistory.Contains(ns::AutoReleased<FAGXTexture>(Texture));
}

bool FAGXCommandEncoder::HasBufferBindingHistory(FAGXBuffer const& Buffer) const
{
	return BufferBindingHistory.Contains(ns::AutoReleased<FAGXBuffer>(Buffer));
}

#pragma mark - Private Functions -

void FAGXCommandEncoder::FenceResource(mtlpp::Texture const& Resource)
{
}

void FAGXCommandEncoder::FenceResource(mtlpp::Buffer const& Resource)
{
}

void FAGXCommandEncoder::SetShaderBufferInternal(mtlpp::FunctionType Function, uint32 Index)
{
	FAGXBufferBindings& Binding = ShaderBuffers[uint32(Function)];

	NSUInteger Offset = Binding.Offsets[Index];
	
	bool bBufferHasBytes = Binding.Bytes[Index] != nil;
	if (!Binding.Buffers[Index] && bBufferHasBytes && !bSupportsMetalFeaturesSetBytes)
	{
		uint8 const* Bytes = (((uint8 const*)Binding.Bytes[Index]->Data) + Binding.Offsets[Index]);
		uint32 Len = Binding.Bytes[Index]->Len - Binding.Offsets[Index];
		
		Offset = 0;
		Binding.Buffers[Index] = RingBuffer.NewBuffer(Len, BufferOffsetAlignment);
		
		FMemory::Memcpy(((uint8*)Binding.Buffers[Index].GetContents()) + Offset, Bytes, Len);
	}
	
	ns::AutoReleased<FAGXBuffer>& Buffer = Binding.Buffers[Index];
	if (Buffer)
	{
#if METAL_DEBUG_OPTIONS
		if(CommandList.GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation)
		{
			ActiveBuffers.Add(Buffer);
		}
#endif
        
		switch (Function)
		{
			case mtlpp::FunctionType::Vertex:
				Binding.Bound |= (1 << Index);
				check(RenderCommandEncoder);
				FenceResource(Buffer);
				[RenderCommandEncoder setVertexBuffer:Buffer.GetPtr() offset:(Offset + Buffer.GetOffset()) atIndex:Index];
				break;

			case mtlpp::FunctionType::Fragment:
				Binding.Bound |= (1 << Index);
				check(RenderCommandEncoder);
				[RenderCommandEncoder setFragmentBuffer:Buffer.GetPtr() offset:(Offset + Buffer.GetOffset()) atIndex:Index];
				break;

			case mtlpp::FunctionType::Kernel:
				Binding.Bound |= (1 << Index);
				check(ComputeCommandEncoder);
				FenceResource(Buffer);
				[ComputeCommandEncoder setBuffer:Buffer.GetPtr() offset:(Offset + Buffer.GetOffset()) atIndex:Index];
				break;

			default:
				check(false);
				break;
		}
		
		if (Buffer.IsSingleUse())
		{
			Binding.Usage[Index] = 0;
			Binding.Offsets[Index] = 0;
			Binding.Buffers[Index] = nil;
			Binding.Bound &= ~(1 << Index);
		}
	}
	else if (bBufferHasBytes && bSupportsMetalFeaturesSetBytes)
	{
		uint8 const* Bytes = (((uint8 const*)Binding.Bytes[Index]->Data) + Binding.Offsets[Index]);
		uint32 Len = Binding.Bytes[Index]->Len - Binding.Offsets[Index];
		
		switch (Function)
		{
			case mtlpp::FunctionType::Vertex:
				Binding.Bound |= (1 << Index);
				check(RenderCommandEncoder);
				[RenderCommandEncoder setVertexBytes:static_cast<const void*>(Bytes) length:(NSUInteger)Len atIndex:Index];
				break;

			case mtlpp::FunctionType::Fragment:
				Binding.Bound |= (1 << Index);
				check(RenderCommandEncoder);
				[RenderCommandEncoder setFragmentBytes:static_cast<const void*>(Bytes) length:(NSUInteger)Len atIndex:Index];
				break;

			case mtlpp::FunctionType::Kernel:
				Binding.Bound |= (1 << Index);
				check(ComputeCommandEncoder);
				[ComputeCommandEncoder setBytes:static_cast<const void*>(Bytes) length:(NSUInteger)Len atIndex:Index];
				break;

			default:
				check(false);
				break;
		}
	}
}
