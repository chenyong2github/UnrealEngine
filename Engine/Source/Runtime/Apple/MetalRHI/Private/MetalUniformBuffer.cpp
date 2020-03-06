// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalConstantBuffer.cpp: Metal Constant buffer implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalProfiler.h"
#include "MetalBuffer.h"
#include "MetalCommandBuffer.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/ScopeRWLock.h"

@implementation FMetalIAB

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FMetalIAB)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
		((FMetalIAB*)Self)->UpdateIAB = 0;
	return Self;
}
-(void)dealloc
{
	SafeReleaseMetalBuffer(IndirectArgumentBuffer);
	SafeReleaseMetalBuffer(IndirectArgumentBufferSideTable);
	[super dealloc];
}
@end

FMetalUniformBuffer::FMetalUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage InUsage, EUniformBufferValidation Validation)
	: FRHIUniformBuffer(Layout)
    , FMetalRHIBuffer(Layout.ConstantBufferSize, (FMetalCommandQueue::SupportsFeature(EMetalFeaturesIABs) && Layout.Resources.Num() ? (EMetalBufferUsage_GPUOnly|BUF_Volatile) : BUF_Volatile), RRT_UniformBuffer)
{
	uint32 NumResources = Layout.Resources.Num();
	if (NumResources)
	{
		ResourceTable.Empty(NumResources);
		ResourceTable.AddZeroed(NumResources);
		
		for (uint32 i = 0; i < NumResources; ++i)
		{
			FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + Layout.Resources[i].MemberOffset);
			
			// Allow null SRV's in uniform buffers for feature levels that don't support SRV's in shaders
			if (Validation == EUniformBufferValidation::ValidateResources && !(GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1 && Layout.Resources[i].MemberType == UBMT_SRV))
			{
				check(Resource);
			}
			
			ResourceTable[i] = Resource;
		}
	}

    uint32 ConstantSize = Layout.ConstantBufferSize;
	if (ConstantSize > 0)
	{
		UE_CLOG(ConstantSize > 65536, LogMetal, Fatal, TEXT("Trying to allocated a uniform layout of size %d that is greater than the maximum permitted 64k."), ConstantSize);
		
		if (Buffer)
		{
			FMemory::Memcpy(Buffer.GetContents(), Contents, ConstantSize);
#if PLATFORM_MAC
			if(Mode == mtlpp::StorageMode::Managed)
			{
				MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, DidModify(ns::Range(0, ConstantSize)));
			}
#endif
		}
		else
		{
			check(Data && Data->Data);
			FMemory::Memcpy(Data->Data, Contents, ConstantSize);
		}
	}

    UpdateResourceTable(ResourceTable, Validation);
}

void const* FMetalUniformBuffer::GetData()
{
	if (Data)
	{
		return Data->Data;
	}
	else if (Buffer)
	{
		return MTLPP_VALIDATE(mtlpp::Buffer, Buffer, SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelValidation, GetContents());
	}
	else
	{
		return nullptr;
	}
}

void FMetalUniformBuffer::UpdateResourceTable(TArray<TRefCountPtr<FRHIResource>>& Resources, EUniformBufferValidation Validation)
{
	ResourceTable = Resources;
}

void FMetalUniformBuffer::Update(const void* Contents, TArray<TRefCountPtr<FRHIResource>>& Resources, EUniformBufferValidation Validation)
{
    uint32 ConstantSize = FRHIUniformBuffer::GetSize();
    if (ConstantSize > 0)
    {
        UE_CLOG(ConstantSize > 65536, LogMetal, Fatal, TEXT("Trying to allocated a uniform layout of size %d that is greater than the maximum permitted 64k."), ConstantSize);
        
		ns::AutoReleased<FMetalBuffer> Buf(Buffer);
		
		void* Data = Lock(true, RLM_WriteOnly, 0, 0, true);
        FMemory::Memcpy(Data, Contents, ConstantSize);
        Unlock();

		ConditionalSetUniformBufferFrameIndex();
	}
	
	UpdateResourceTable(Resources, Validation);
}

FUniformBufferRHIRef FMetalDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
	@autoreleasepool {
	check(IsInRenderingThread() || IsInParallelRenderingThread() || IsInRHIThread());
		return new FMetalUniformBuffer(Contents, Layout, Usage, Validation);
	}
}

struct FMetalRHICommandUpateUniformBuffer : public FRHICommand<FMetalRHICommandUpateUniformBuffer>
{
	TRefCountPtr<FMetalUniformBuffer> Buffer;
	TArray<TRefCountPtr<FRHIResource> > ResourceTable;
	char* Contents;
	
	FORCEINLINE_DEBUGGABLE FMetalRHICommandUpateUniformBuffer(FMetalUniformBuffer* InBuffer, void const* Data, TArray<TRefCountPtr<FRHIResource>>& Resources)
	: Buffer(InBuffer)
	, ResourceTable(Resources)
	, Contents(nullptr)
	{
		uint32 MaxLayoutSize = InBuffer->GetSize();
		Contents = new char[MaxLayoutSize];
		FMemory::Memcpy(Contents, Data, MaxLayoutSize);
	}
	
	virtual ~FMetalRHICommandUpateUniformBuffer()
	{
		delete [] Contents;
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		Buffer->Update(Contents, ResourceTable, EUniformBufferValidation::None);
	}
};

void FMetalDynamicRHI::RHIUpdateUniformBuffer(FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
	@autoreleasepool {
	// check((IsInRenderingThread() || IsInRHIThread()) && !IsInParallelRenderingThread());

	FMetalUniformBuffer* UniformBuffer = ResourceCast(UniformBufferRHI);
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
		
	TArray<TRefCountPtr<FRHIResource> > ResourceTable;
	ResourceTable.AddZeroed(UniformBuffer->GetLayout().Resources.Num());
		
	const FRHIUniformBufferLayout& Layout = UniformBuffer->GetLayout();
		
	for (uint32 i = 0; i < UniformBuffer->GetLayout().Resources.Num(); ++i)
	{
		FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + Layout.Resources[i].MemberOffset);
		ResourceTable[i] = Resource;
	}	
		
	if (RHICmdList.Bypass() || !IsRunningRHIInSeparateThread())
	{
		UniformBuffer->Update(Contents, ResourceTable, EUniformBufferValidation::None);
	}
	else
	{
		new (RHICmdList.AllocCommand<FMetalRHICommandUpateUniformBuffer>()) FMetalRHICommandUpateUniformBuffer(UniformBuffer, Contents, ResourceTable);
		RHICmdList.RHIThreadFence(true);
	}
	}
}
