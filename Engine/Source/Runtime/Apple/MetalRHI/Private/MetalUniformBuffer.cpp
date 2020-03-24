// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalConstantBuffer.cpp: Metal Constant buffer implementation.
=============================================================================*/

#include "MetalRHI.h"
#include "MetalResources.h"
#include "MetalFrameAllocator.h"
#include "MetalRHIPrivate.h"

#pragma mark Suballocated Uniform Buffer Implementation

FMetalSuballocatedUniformBuffer::FMetalSuballocatedUniformBuffer(const FRHIUniformBufferLayout& Layout, EUniformBufferUsage Usage, EUniformBufferValidation InValidation)
    : FRHIUniformBuffer(Layout)
    , LastFrameUpdated(0)
    , Offset(0)
    , Backing(nil)
    , Shadow(nullptr)
    , ResourceTable()
#if !UE_BUILD_SHIPPING
    , Validation(InValidation)
#endif
{
    // Slate can create SingleDraw uniform buffers and use them several frames later. So it must be included.
    if(Usage == UniformBuffer_SingleDraw || Usage == UniformBuffer_MultiFrame)
    {
        Shadow = FMemory::Malloc(this->GetSize());
    }
}

FMetalSuballocatedUniformBuffer::~FMetalSuballocatedUniformBuffer()
{
    if(this->HasShadow())
    {
        FMemory::Free(Shadow);
    }
    
    // Note: this object does NOT own a reference
    // to the uniform buffer backing store
}

bool FMetalSuballocatedUniformBuffer::HasShadow()
{
    return this->Shadow != nullptr;
}

void FMetalSuballocatedUniformBuffer::Update(const void* Contents)
{
    if(this->HasShadow())
    {
        FMemory::Memcpy(this->Shadow, Contents, this->GetSize());
    }
    
    const FRHIUniformBufferLayout& Layout = this->GetLayout();
    uint32 NumResources = Layout.Resources.Num();
    if (NumResources > 0)
    {
        ResourceTable.Empty(NumResources);
        ResourceTable.AddZeroed(NumResources);
        
        for (uint32 i = 0; i < NumResources; ++i)
        {
            FRHIResource* Resource = *(FRHIResource**)((uint8*)Contents + Layout.Resources[i].MemberOffset);
            
            // Allow null SRV's in uniform buffers for feature levels that don't support SRV's in shaders
#if METAL_UNIFORM_BUFFER_VALIDATION
            if (Validation == EUniformBufferValidation::ValidateResources && !(GMaxRHIFeatureLevel <= ERHIFeatureLevel::ES3_1 && GetLayout().Resources[i].MemberType == UBMT_SRV))
            {
                check(Resource);
            }
#endif
            
            ResourceTable[i] = Resource;
        }
    }

    this->PushToGPUBacking(Contents);
}

// Acquires a region in the current frame's uniform buffer and
// pushes the data in Contents into that GPU backing store
// The amount of data read from Contents is given by the Layout
void FMetalSuballocatedUniformBuffer::PushToGPUBacking(const void* Contents)
{
    check(IsInRenderingThread() ^ IsRunningRHIInSeparateThread());
    
    FMetalDeviceContext& DeviceContext = GetMetalDeviceContext();
    
    FMetalFrameAllocator* Allocator = DeviceContext.GetUniformAllocator();
    FMetalFrameAllocator::AllocationEntry Entry = Allocator->AcquireSpace(this->GetSize());
    // copy contents into backing
    this->Backing = Entry.Backing;
    this->Offset = Entry.Offset;
    uint8* ConstantSpace = reinterpret_cast<uint8*>([this->Backing contents]) + Entry.Offset;
    FMemory::Memcpy(ConstantSpace, Contents, this->GetSize());
    LastFrameUpdated = DeviceContext.GetFrameNumberRHIThread();
}

// Because we can create a uniform buffer on frame N and may not bind it until frame N+10
// we need to keep a copy of the most recent data. Then when it's time to bind this
// uniform buffer we can push the data into the GPU backing.
void FMetalSuballocatedUniformBuffer::PrepareToBind()
{
    FMetalDeviceContext& DeviceContext = GetMetalDeviceContext();
    if(Shadow && LastFrameUpdated < DeviceContext.GetFrameNumberRHIThread())
    {
        this->PushToGPUBacking(this->Shadow);
    }
}
