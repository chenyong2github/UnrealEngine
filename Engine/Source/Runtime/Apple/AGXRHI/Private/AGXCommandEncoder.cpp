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
, RingBuffer(EncoderRingBufferSize, BufferOffsetAlignment, FAGXCommandQueue::GetCompatibleResourceOptions((mtlpp::ResourceOptions)(mtlpp::ResourceOptions::HazardTrackingModeUntracked | BUFFER_RESOURCE_STORAGE_MANAGED)))
, RenderPassDesc(nil)
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
		ColorStoreActions[i] = mtlpp::StoreAction::Unknown;
	}
	DepthStoreAction = mtlpp::StoreAction::Unknown;
	StencilStoreAction = mtlpp::StoreAction::Unknown;
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
			ColorStoreActions[i] = mtlpp::StoreAction::Unknown;
		}
		DepthStoreAction = mtlpp::StoreAction::Unknown;
		StencilStoreAction = mtlpp::StoreAction::Unknown;
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
			RenderCommandEncoder.SetVertexBuffer(nil, 0, i);
			RenderCommandEncoder.SetFragmentBuffer(nil, 0, i);
		}
		
		for (uint32 i = 0; i < ML_MaxTextures; i++)
		{
			RenderCommandEncoder.SetVertexTexture(nil, i);
			RenderCommandEncoder.SetFragmentTexture(nil, i);
		}
	}
	else if (IsComputeCommandEncoderActive())
	{
		for (uint32 i = 0; i < ML_MaxBuffers; i++)
		{
			ComputeCommandEncoder.SetBuffer(nil, 0, i);
		}
		
		for (uint32 i = 0; i < ML_MaxTextures; i++)
		{
			ComputeCommandEncoder.SetTexture(nil, i);
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
	
bool FAGXCommandEncoder::IsParallelRenderCommandEncoderActive(void) const
{
	return ParallelRenderCommandEncoder.GetPtr() != nil;
}
	
bool FAGXCommandEncoder::IsRenderCommandEncoderActive(void) const
{
	return RenderCommandEncoder.GetPtr() != nil || ParallelRenderCommandEncoder.GetPtr() != nil;
}

bool FAGXCommandEncoder::IsComputeCommandEncoderActive(void) const
{
	return ComputeCommandEncoder.GetPtr() != nil;
}

bool FAGXCommandEncoder::IsBlitCommandEncoderActive(void) const
{
	return BlitCommandEncoder.GetPtr() != nil;
}

bool FAGXCommandEncoder::IsImmediate(void) const
{
	return CommandList.IsImmediate();
}

bool FAGXCommandEncoder::IsParallel(void) const
{
	return CommandList.IsParallel() && (Type == EAGXCommandEncoderCurrent);
}

bool FAGXCommandEncoder::IsRenderPassDescriptorValid(void) const
{
	return (RenderPassDesc != nil);
}

mtlpp::RenderPassDescriptor const& FAGXCommandEncoder::GetRenderPassDescriptor(void) const
{
	return RenderPassDesc;
}

mtlpp::ParallelRenderCommandEncoder& FAGXCommandEncoder::GetParallelRenderCommandEncoder(void)
{
	return ParallelRenderCommandEncoder;
}

mtlpp::RenderCommandEncoder& FAGXCommandEncoder::GetChildRenderCommandEncoder(uint32 Index)
{
	check(IsParallelRenderCommandEncoderActive() && Index < ChildRenderCommandEncoders.Num());
	return ChildRenderCommandEncoders[Index];
}

mtlpp::RenderCommandEncoder& FAGXCommandEncoder::GetRenderCommandEncoder(void)
{
	check(IsRenderCommandEncoderActive() && RenderCommandEncoder);
	return RenderCommandEncoder;
}

mtlpp::ComputeCommandEncoder& FAGXCommandEncoder::GetComputeCommandEncoder(void)
{
	check(IsComputeCommandEncoderActive());
	return ComputeCommandEncoder;
}

mtlpp::BlitCommandEncoder& FAGXCommandEncoder::GetBlitCommandEncoder(void)
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
	
	ParallelRenderCommandEncoder = MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ParallelRenderCommandEncoder(RenderPassDesc));
	
	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"ParallelRenderCommandEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		ParallelRenderCommandEncoder.SetLabel(Label);
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				ParallelRenderCommandEncoder.PushDebugGroup(Group);
			}
		}
	}
	
	for (uint32 i = 0; i < NumChildren; i++)
	{
		mtlpp::RenderCommandEncoder CommandEncoder = MTLPP_VALIDATE(mtlpp::ParallelRenderCommandEncoder, ParallelRenderCommandEncoder, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, GetRenderCommandEncoder());
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
		RenderCommandEncoder = MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, RenderCommandEncoder(RenderPassDesc));
		EncoderNum++;	
	}
	else
	{
		RenderCommandEncoder = GetAGXDeviceContext().GetParallelRenderCommandEncoder(CommandList.GetParallelIndex(), ParallelRenderCommandEncoder, CommandBuffer);
	}
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"RenderEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		RenderCommandEncoder.SetLabel(Label);
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				RenderCommandEncoder.PushDebugGroup(Group);
			}
		}
	}
}

void FAGXCommandEncoder::BeginComputeCommandEncoding(mtlpp::DispatchType DispatchType)
{
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	if (DispatchType == mtlpp::DispatchType::Serial)
	{
		ComputeCommandEncoder = MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ComputeCommandEncoder());
	}
	else
	{
		ComputeCommandEncoder = MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, ComputeCommandEncoder(DispatchType));
	}

	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"ComputeEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		ComputeCommandEncoder.SetLabel(Label);
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				ComputeCommandEncoder.PushDebugGroup(Group);
			}
		}
	}
}

void FAGXCommandEncoder::BeginBlitCommandEncoding(void)
{
	check(CommandBuffer);
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	
	BlitCommandEncoder = MTLPP_VALIDATE(mtlpp::CommandBuffer, CommandBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, BlitCommandEncoder());
	
	EncoderNum++;
	
	NSString* Label = nil;
	
	if(GetEmitDrawEvents())
	{
		Label = [NSString stringWithFormat:@"BlitEncoder: %@", [DebugGroups count] > 0 ? [DebugGroups lastObject] : (NSString*)CFSTR("InitialPass")];
		BlitCommandEncoder.SetLabel(Label);
		
		if([DebugGroups count])
		{
			for (NSString* Group in DebugGroups)
			{
				BlitCommandEncoder.PushDebugGroup(Group);
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
				if (ParallelRenderCommandEncoder.GetPtr() == nil)
				{
					check(RenderPassDesc);
					
					ns::Array<mtlpp::RenderPassColorAttachmentDescriptor> ColorAttachments = RenderPassDesc.GetColorAttachments();
					for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
					{
						if (ColorAttachments[i].GetTexture() && ColorAttachments[i].GetStoreAction() == mtlpp::StoreAction::Unknown)
						{
							mtlpp::StoreAction Action = ColorStoreActions[i];
							check(Action != mtlpp::StoreAction::Unknown);
							RenderCommandEncoder.SetColorStoreAction((mtlpp::StoreAction)Action, i);
						}
					}
					if (RenderPassDesc.GetDepthAttachment().GetTexture() && RenderPassDesc.GetDepthAttachment().GetStoreAction() == mtlpp::StoreAction::Unknown)
					{
						mtlpp::StoreAction Action = DepthStoreAction;
						check(Action != mtlpp::StoreAction::Unknown);
						RenderCommandEncoder.SetDepthStoreAction((mtlpp::StoreAction)Action);
					}
					if (RenderPassDesc.GetStencilAttachment().GetTexture() && RenderPassDesc.GetStencilAttachment().GetStoreAction() == mtlpp::StoreAction::Unknown)
					{
						mtlpp::StoreAction Action = StencilStoreAction;
						check(Action != mtlpp::StoreAction::Unknown);
						RenderCommandEncoder.SetStencilStoreAction((mtlpp::StoreAction)Action);
					}
				}
				
				RenderCommandEncoder.EndEncoding();
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
				
				ParallelRenderCommandEncoder = nil;
			}

			if (ParallelRenderCommandEncoder && IsImmediate())
			{
				{
					check(RenderPassDesc);
					
					ns::Array<mtlpp::RenderPassColorAttachmentDescriptor> ColorAttachments = RenderPassDesc.GetColorAttachments();
					for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
					{
						if (ColorAttachments[i].GetTexture() && ColorAttachments[i].GetStoreAction() == mtlpp::StoreAction::Unknown)
						{
							mtlpp::StoreAction Action = ColorStoreActions[i];
							check(Action != mtlpp::StoreAction::Unknown);
							ParallelRenderCommandEncoder.SetColorStoreAction((mtlpp::StoreAction)Action, i);
						}
					}
					if (RenderPassDesc.GetDepthAttachment().GetTexture() && RenderPassDesc.GetDepthAttachment().GetStoreAction() == mtlpp::StoreAction::Unknown)
					{
						mtlpp::StoreAction Action = DepthStoreAction;
						check(Action != mtlpp::StoreAction::Unknown);
						ParallelRenderCommandEncoder.SetDepthStoreAction((mtlpp::StoreAction)Action);
					}
					if (RenderPassDesc.GetStencilAttachment().GetTexture() && RenderPassDesc.GetStencilAttachment().GetStoreAction() == mtlpp::StoreAction::Unknown)
					{
						mtlpp::StoreAction Action = StencilStoreAction;
						check(Action != mtlpp::StoreAction::Unknown);
						ParallelRenderCommandEncoder.SetStencilStoreAction((mtlpp::StoreAction)Action);
					}
				}

				ParallelRenderCommandEncoder.EndEncoding();
				ParallelRenderCommandEncoder = nil;

				ChildRenderCommandEncoders.Empty();
			}
		}
		else if(IsComputeCommandEncoderActive())
		{
			ComputeCommandEncoder.EndEncoding();
			ComputeCommandEncoder = nil;
		}
		else if(IsBlitCommandEncoderActive())
		{
			BlitCommandEncoder.EndEncoding();
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
			RenderCommandEncoder.InsertDebugSignpost(String);
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			ParallelRenderCommandEncoder.InsertDebugSignpost(String);
			METAL_DEBUG_LAYER(EAGXDebugLevelFastValidation, ParallelRenderCommandEncoder.InsertDebugSignpost(String));
		}
		else if (ComputeCommandEncoder)
		{
			ComputeCommandEncoder.InsertDebugSignpost(String);
		}
		else if (BlitCommandEncoder)
		{
			BlitCommandEncoder.InsertDebugSignpost(String);
		}
	}
}

void FAGXCommandEncoder::PushDebugGroup(ns::String const& String)
{
	if (String)
	{
		[DebugGroups addObject:String];
		if (RenderCommandEncoder)
		{
			RenderCommandEncoder.PushDebugGroup(String);
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			ParallelRenderCommandEncoder.PushDebugGroup(String);
			METAL_DEBUG_LAYER(EAGXDebugLevelFastValidation, ParallelRenderCommandEncoder.PushDebugGroup(String));
		}
		else if (ComputeCommandEncoder)
		{
			ComputeCommandEncoder.PushDebugGroup(String);
		}
		else if (BlitCommandEncoder)
		{
			BlitCommandEncoder.PushDebugGroup(String);
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
			RenderCommandEncoder.PopDebugGroup();
		}
		else if (ParallelRenderCommandEncoder && !IsParallel())
		{
			ParallelRenderCommandEncoder.PopDebugGroup();
			METAL_DEBUG_LAYER(EAGXDebugLevelFastValidation, ParallelRenderCommandEncoder.PopDebugGroup());
		}
		else if (ComputeCommandEncoder)
		{
			ComputeCommandEncoder.PopDebugGroup();
		}
		else if (BlitCommandEncoder)
		{
			BlitCommandEncoder.PopDebugGroup();
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

void FAGXCommandEncoder::SetRenderPassDescriptor(mtlpp::RenderPassDescriptor RenderPass)
{
	check(IsRenderCommandEncoderActive() == false && IsComputeCommandEncoderActive() == false && IsBlitCommandEncoderActive() == false);
	check(RenderPass);
	
	if(RenderPass.GetPtr() != RenderPassDesc.GetPtr())
	{
		AGXSafeReleaseMetalRenderPassDescriptor(RenderPassDesc);
		RenderPassDesc = RenderPass;
		{
			for (uint32 i = 0; i < MaxSimultaneousRenderTargets; i++)
			{
				ColorStoreActions[i] = mtlpp::StoreAction::Unknown;
			}
			DepthStoreAction = mtlpp::StoreAction::Unknown;
			StencilStoreAction = mtlpp::StoreAction::Unknown;
		}
	}
	check(RenderPassDesc);
	
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

void FAGXCommandEncoder::SetRenderPassStoreActions(mtlpp::StoreAction const* const ColorStore, mtlpp::StoreAction const DepthStore, mtlpp::StoreAction const StencilStore)
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
	check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetRenderPipelineState(PipelineState->RenderPipelineState);
	}
}

void FAGXCommandEncoder::SetViewport(mtlpp::Viewport const Viewport[], uint32 NumActive)
{
	check(RenderCommandEncoder);
	check(NumActive >= 1 && NumActive < ML_MaxViewports);
	if (NumActive == 1)
	{
		RenderCommandEncoder.SetViewport(Viewport[0]);
	}
#if PLATFORM_MAC
	else
	{
		check(FAGXCommandQueue::SupportsFeature(EAGXFeaturesMultipleViewports));
		RenderCommandEncoder.SetViewports(Viewport, NumActive);
	}
#endif
}

void FAGXCommandEncoder::SetFrontFacingWinding(mtlpp::Winding const InFrontFacingWinding)
{
    check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetFrontFacingWinding(InFrontFacingWinding);
	}
}

void FAGXCommandEncoder::SetCullMode(mtlpp::CullMode const InCullMode)
{
    check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetCullMode(InCullMode);
	}
}

void FAGXCommandEncoder::SetDepthBias(float const InDepthBias, float const InSlopeScale, float const InClamp)
{
    check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetDepthBias(InDepthBias, InSlopeScale, InClamp);
	}
}

void FAGXCommandEncoder::SetScissorRect(mtlpp::ScissorRect const Rect[], uint32 NumActive)
{
    check(RenderCommandEncoder);
	check(NumActive >= 1 && NumActive < ML_MaxViewports);
	if (NumActive == 1)
	{
		RenderCommandEncoder.SetScissorRect(Rect[0]);
	}
#if PLATFORM_MAC
	else
	{
		check(FAGXCommandQueue::SupportsFeature(EAGXFeaturesMultipleViewports));
		RenderCommandEncoder.SetScissorRects(Rect, NumActive);
	}
#endif
}

void FAGXCommandEncoder::SetTriangleFillMode(mtlpp::TriangleFillMode const InFillMode)
{
    check(RenderCommandEncoder);
	{
		RenderCommandEncoder.SetTriangleFillMode(InFillMode);
	}
}

void FAGXCommandEncoder::SetBlendColor(float const Red, float const Green, float const Blue, float const Alpha)
{
	check(RenderCommandEncoder);
	{
		RenderCommandEncoder.SetBlendColor(Red, Green, Blue, Alpha);
	}
}

void FAGXCommandEncoder::SetDepthStencilState(mtlpp::DepthStencilState const& InDepthStencilState)
{
    check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetDepthStencilState(InDepthStencilState);
	}
}

void FAGXCommandEncoder::SetStencilReferenceValue(uint32 const ReferenceValue)
{
    check (RenderCommandEncoder);
	{
		RenderCommandEncoder.SetStencilReferenceValue(ReferenceValue);
	}
}

void FAGXCommandEncoder::SetVisibilityResultMode(mtlpp::VisibilityResultMode const Mode, NSUInteger const Offset)
{
    check (RenderCommandEncoder);
	{
		check(Mode == mtlpp::VisibilityResultMode::Disabled || RenderPassDesc.GetVisibilityResultBuffer());
		RenderCommandEncoder.SetVisibilityResultMode(Mode, Offset);
	}
}
	
#pragma mark - Public Shader Resource Mutators -

void FAGXCommandEncoder::SetShaderBuffer(mtlpp::FunctionType const FunctionType, FAGXBuffer const& Buffer, NSUInteger const Offset, NSUInteger const Length, NSUInteger index, mtlpp::ResourceUsage const Usage, EPixelFormat const Format, NSUInteger const ElementRowPitch)
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
	ShaderBuffers[uint32(FunctionType)].Usage[Index] = mtlpp::ResourceUsage::Read;
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
					RenderCommandEncoder.SetVertexData(Bytes, Length, Index);
					break;
				case mtlpp::FunctionType::Fragment:
					check(RenderCommandEncoder);
					RenderCommandEncoder.SetFragmentData(Bytes, Length, Index);
					break;
				case mtlpp::FunctionType::Kernel:
					check(ComputeCommandEncoder);
					ComputeCommandEncoder.SetBytes(Bytes, Length, Index);
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
		ShaderBuffers[uint32(FunctionType)].Usage[Index] = mtlpp::ResourceUsage::Read;
		ShaderBuffers[uint32(FunctionType)].SetBufferMetaData(Index, Length, GAGXBufferFormats[PF_Unknown].DataFormat, 0);
	}
	else
	{
		ShaderBuffers[uint32(FunctionType)].Bound &= ~(1 << Index);
		
		ShaderBuffers[uint32(FunctionType)].Buffers[Index] = nil;
		ShaderBuffers[uint32(FunctionType)].Bytes[Index] = nil;
		ShaderBuffers[uint32(FunctionType)].Offsets[Index] = 0;
		ShaderBuffers[uint32(FunctionType)].Usage[Index] = mtlpp::ResourceUsage(0);
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
			RenderCommandEncoder.SetVertexBufferOffset(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset(), index);
			break;
		case mtlpp::FunctionType::Fragment:
			check(RenderCommandEncoder);
			RenderCommandEncoder.SetFragmentBufferOffset(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset(), index);
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			ComputeCommandEncoder.SetBufferOffset(Offset + ShaderBuffers[uint32(FunctionType)].Buffers[index].GetOffset(), index);
			break;
		default:
			check(false);
			break;
	}
}

void FAGXCommandEncoder::SetShaderTexture(mtlpp::FunctionType FunctionType, FAGXTexture const& Texture, NSUInteger index, mtlpp::ResourceUsage Usage)
{
	check(index < ML_MaxTextures);
	switch (FunctionType)
	{
		case mtlpp::FunctionType::Vertex:
			check (RenderCommandEncoder);
			FenceResource(Texture);
			RenderCommandEncoder.SetVertexTexture(Texture, index);
			break;
		case mtlpp::FunctionType::Fragment:
			check(RenderCommandEncoder);
			RenderCommandEncoder.SetFragmentTexture(Texture, index);
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			FenceResource(Texture);
			ComputeCommandEncoder.SetTexture(Texture, index);
			break;
		default:
			check(false);
			break;
	}
	
	if (Texture)
	{
		uint8 Swizzle[4] = {0,0,0,0};
		assert(sizeof(Swizzle) == sizeof(uint32));
		if (Texture.GetPixelFormat() == mtlpp::PixelFormat::X32_Stencil8
#if PLATFORM_MAC
		 ||	Texture.GetPixelFormat() == mtlpp::PixelFormat::X24_Stencil8
#endif
		)
		{
			Swizzle[0] = Swizzle[1] = Swizzle[2] = Swizzle[3] = 1;
		}
		ShaderBuffers[uint32(FunctionType)].SetTextureSwizzle(index, Swizzle);
		TextureBindingHistory.Add(ns::AutoReleased<FAGXTexture>(Texture));
	}
}

void FAGXCommandEncoder::SetShaderSamplerState(mtlpp::FunctionType FunctionType, mtlpp::SamplerState const& Sampler, NSUInteger index)
{
	check(index < ML_MaxSamplers);
	switch (FunctionType)
	{
		case mtlpp::FunctionType::Vertex:
       		check (RenderCommandEncoder);
			RenderCommandEncoder.SetVertexSamplerState(Sampler, index);
			break;
		case mtlpp::FunctionType::Fragment:
			check (RenderCommandEncoder);
			RenderCommandEncoder.SetFragmentSamplerState(Sampler, index);
			break;
		case mtlpp::FunctionType::Kernel:
			check (ComputeCommandEncoder);
			ComputeCommandEncoder.SetSamplerState(Sampler, index);
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
		ComputeCommandEncoder.SetComputePipelineState(State->ComputePipelineState);
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
	NSUInteger Offset = ShaderBuffers[uint32(Function)].Offsets[Index];
	mtlpp::ResourceUsage Usage = ShaderBuffers[uint32(Function)].Usage[Index];
	bool bBufferHasBytes = ShaderBuffers[uint32(Function)].Bytes[Index] != nil;
	if (!ShaderBuffers[uint32(Function)].Buffers[Index] && bBufferHasBytes && !bSupportsMetalFeaturesSetBytes)
	{
		uint8 const* Bytes = (((uint8 const*)ShaderBuffers[uint32(Function)].Bytes[Index]->Data) + ShaderBuffers[uint32(Function)].Offsets[Index]);
		uint32 Len = ShaderBuffers[uint32(Function)].Bytes[Index]->Len - ShaderBuffers[uint32(Function)].Offsets[Index];
		
		Offset = 0;
		ShaderBuffers[uint32(Function)].Buffers[Index] = RingBuffer.NewBuffer(Len, BufferOffsetAlignment);
		
		FMemory::Memcpy(((uint8*)ShaderBuffers[uint32(Function)].Buffers[Index].GetContents()) + Offset, Bytes, Len);
	}
	
	ns::AutoReleased<FAGXBuffer>& Buffer = ShaderBuffers[uint32(Function)].Buffers[Index];
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
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(RenderCommandEncoder);
				FenceResource(Buffer);
				RenderCommandEncoder.SetVertexBuffer(Buffer, Offset, Index);
				break;
			case mtlpp::FunctionType::Fragment:
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(RenderCommandEncoder);
				RenderCommandEncoder.SetFragmentBuffer(Buffer, Offset, Index);
				break;
			case mtlpp::FunctionType::Kernel:
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(ComputeCommandEncoder);
				FenceResource(Buffer);
				ComputeCommandEncoder.SetBuffer(Buffer, Offset, Index);
				break;
			default:
				check(false);
				break;
		}
		
		if (Buffer.IsSingleUse())
		{
			ShaderBuffers[uint32(Function)].Usage[Index] = mtlpp::ResourceUsage(0);
			ShaderBuffers[uint32(Function)].Offsets[Index] = 0;
			ShaderBuffers[uint32(Function)].Buffers[Index] = nil;
			ShaderBuffers[uint32(Function)].Bound &= ~(1 << Index);
		}
	}
	else if (bBufferHasBytes && bSupportsMetalFeaturesSetBytes)
	{
		uint8 const* Bytes = (((uint8 const*)ShaderBuffers[uint32(Function)].Bytes[Index]->Data) + ShaderBuffers[uint32(Function)].Offsets[Index]);
		uint32 Len = ShaderBuffers[uint32(Function)].Bytes[Index]->Len - ShaderBuffers[uint32(Function)].Offsets[Index];
		
		switch (Function)
		{
			case mtlpp::FunctionType::Vertex:
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(RenderCommandEncoder);
				RenderCommandEncoder.SetVertexData(Bytes, Len, Index);
				break;
			case mtlpp::FunctionType::Fragment:
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(RenderCommandEncoder);
				RenderCommandEncoder.SetFragmentData(Bytes, Len, Index);
				break;
			case mtlpp::FunctionType::Kernel:
				ShaderBuffers[uint32(Function)].Bound |= (1 << Index);
				check(ComputeCommandEncoder);
				ComputeCommandEncoder.SetBytes(Bytes, Len, Index);
				break;
			default:
				check(false);
				break;
		}
	}
}
