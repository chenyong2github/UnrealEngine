// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXVertexBuffer.cpp: AGX vertex buffer RHI implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXProfiler.h"
#include "AGXCommandQueue.h"
#include "Containers/ResourceArray.h"
#include "RenderUtils.h"
#include "AGXLLM.h"
#include <objc/runtime.h>

#define METAL_POOL_BUFFER_BACKING 1

#if !METAL_POOL_BUFFER_BACKING
DECLARE_MEMORY_STAT(TEXT("Used Device Buffer Memory"), STAT_AGXDeviceBufferMemory, STATGROUP_AGXRHI);
#endif

#if STATS
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size, Usage) \
	switch(Type)	{ \
		case RRT_UniformBuffer: INC_DWORD_STAT_BY(STAT_AGXUniform##Name, Size); break; \
        case RRT_Buffer: if (EnumHasAnyFlags(Usage, BUF_IndexBuffer)){ INC_DWORD_STAT_BY(STAT_AGXIndex##Name, Size); } else { INC_DWORD_STAT_BY(STAT_AGXVertex##Name, Size); } break; \
		default: break; \
	}
#else
#define METAL_INC_DWORD_STAT_BY(Type, Name, Size, Usage)
#endif

@implementation FAGXBufferData

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXBufferData)

-(instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		self->Data = nullptr;
		self->Len = 0;
	}
	return Self;
}
-(instancetype)initWithSize:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
	}
	return Self;
}
-(instancetype)initWithBytes:(void const*)InData length:(uint32)InSize
{
	id Self = [super init];
	if (Self)
	{
		self->Data = (uint8*)FMemory::Malloc(InSize);
		self->Len = InSize;
		check(self->Data);
		FMemory::Memcpy(self->Data, InData, InSize);
	}
	return Self;
}
-(void)dealloc
{
	if (self->Data)
	{
		FMemory::Free(self->Data);
		self->Data = nullptr;
		self->Len = 0;
	}
	[super dealloc];
}
@end

static EAGXBufferUsage AGXBufferUsage(EBufferUsageFlags InUsage)
{
	EAGXBufferUsage Usage = EAGXBufferUsage::None;

	if (EnumHasAnyFlags(InUsage, BUF_VertexBuffer))
	{
		Usage |= EAGXBufferUsage::LinearTex;
	}

	if (EnumHasAnyFlags(InUsage, BUF_IndexBuffer))
	{
		Usage |= (EAGXBufferUsage::GPUOnly | EAGXBufferUsage::LinearTex);
	}

	if (EnumHasAnyFlags(InUsage, BUF_StructuredBuffer))
	{
		Usage |= EAGXBufferUsage::GPUOnly;
	}

	return Usage;
}

void FAGXRHIBuffer::Swap(FAGXRHIBuffer& Other)
{
	::Swap(*this, Other);
}

static bool CanUsePrivateMemory()
{
	return (FAGXCommandQueue::SupportsFeature(EAGXFeaturesEfficientBufferBlits));
}

bool FAGXRHIBuffer::UsePrivateMemory() const
{
	return (FAGXCommandQueue::SupportsFeature(EAGXFeaturesEfficientBufferBlits) && EnumHasAnyFlags(Usage, BUF_Dynamic|BUF_Static));
}

FAGXRHIBuffer::FAGXRHIBuffer(uint32 InSize, EBufferUsageFlags InUsage, EAGXBufferUsage InAgxUsage, ERHIResourceType InType)
: Data(nullptr)
, LastLockFrame(0)
, CurrentIndex(0)
, NumberOfBuffers(0)
, CurrentLockMode(RLM_Num)
, LockOffset(0)
, LockSize(0)
, Size(InSize)
, Usage(InUsage)
, AgxUsage(InAgxUsage)
, StorageMode(BUFFER_STORAGE_MODE)
, Type(InType)
{
	// No life-time usage information? Enforce Dynamic.
	if(!EnumHasAnyFlags(Usage, BUF_Static | BUF_Dynamic | BUF_Volatile))
	{
		Usage |= BUF_Dynamic;
	}
	
	const bool bIsStatic = EnumHasAnyFlags(Usage, BUF_Static);
	const bool bIsDynamic = EnumHasAnyFlags(Usage, BUF_Dynamic);
	const bool bIsVolatile = EnumHasAnyFlags(Usage, BUF_Volatile);
	const bool bWantsView = EnumHasAnyFlags(Usage, BUF_ShaderResource | BUF_UnorderedAccess);
	
	check(bIsStatic ^ bIsDynamic ^ bIsVolatile);

	StorageMode = UsePrivateMemory() ? MTLStorageModePrivate : BUFFER_STORAGE_MODE;
	StorageMode = CanUsePrivateMemory() ? MTLStorageModePrivate : StorageMode;
	
	if (InSize)
	{
		checkf(InSize <= 1024 * 1024 * 1024, TEXT("Metal doesn't support buffers > 1GB"));
		
		// Temporary buffers less than the buffer page size - currently 4Kb - is better off going through the set*Bytes API if available.
		// These can't be used for shader resources or UAVs if we want to use the 'Linear Texture' code path
		if (!(EnumHasAnyFlags(Usage, BUF_UnorderedAccess|BUF_ShaderResource) || EnumHasAnyFlags(InAgxUsage, EAGXBufferUsage::GPUOnly)) && EnumHasAnyFlags(Usage, BUF_Volatile) && InSize < AGXBufferPageSize && (InSize < AGXBufferBytesSize))
		{
			Data = [[FAGXBufferData alloc] initWithSize:InSize];
			METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize, Usage);
		}
		else
		{
			uint32 AllocSize = Size;
			
			// Static buffers will never be discarded. You can update them directly.
			if(bIsStatic)
			{
				NumberOfBuffers = 1;
			}
			else
			{
				check(bIsDynamic || bIsVolatile);
				NumberOfBuffers = 3;
			}
			
			check(NumberOfBuffers > 0);
			
			BufferPool.SetNum(NumberOfBuffers);
			
			// These allocations will not go into the pool.
			uint32 RequestedBufferOffsetAlignment = BufferOffsetAlignment;
			if(bWantsView)
			{
				// Buffer backed linear textures have specific align requirements
				// We don't know upfront the pixel format that may be requested for an SRV so we can't use minimumLinearTextureAlignmentForPixelFormat:
				RequestedBufferOffsetAlignment = BufferBackedLinearTextureOffsetAlignment;
			}
			
			AllocSize = Align(AllocSize, RequestedBufferOffsetAlignment);
			for(uint32 i = 0; i < NumberOfBuffers; i++)
			{
				FAGXBufferAndViews& Backing = BufferPool[i];

				MTLResourceOptions Options = MTLResourceCPUCacheModeDefaultCache | (StorageMode << MTLResourceStorageModeShift) | MTLResourceHazardTrackingModeDefault;
#if METAL_POOL_BUFFER_BACKING
				FAGXPooledBufferArgs ArgsCPU(AllocSize, Usage, Options);
				Backing.Buffer = GetAGXDeviceContext().CreatePooledBuffer(ArgsCPU);
				Backing.Buffer.SetOwner(nullptr, false);
#else
				METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("AllocBuffer: %llu, %llu"), AllocSize, Options)));
				// Allocate one.
				Backing.Buffer = FAGXBuffer(MTLPP_VALIDATE(mtlpp::Device, GMtlppDevice, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, NewBuffer(AllocSize, mtlpp::ResourceOptions(Options))), false);
				
#if STATS || ENABLE_LOW_LEVEL_MEM_TRACKER
				AGXLLM::LogAllocBuffer(Backing.Buffer.GetPtr());
#endif
				INC_MEMORY_STAT_BY(STAT_AGXDeviceBufferMemory, Backing.Buffer.GetLength());
				
				if (GAGXBufferZeroFill && StorageMode != MTLStorageModePrivate)
				{
					FMemory::Memset(((uint8*)Backing.Buffer.GetContents()), 0, Backing.Buffer.GetLength());
				}
				
				METAL_DEBUG_OPTION(GetAGXDeviceContext().ValidateIsInactiveBuffer(Backing.Buffer));
				METAL_FATAL_ASSERT(Backing.Buffer, TEXT("Failed to create buffer of size %u and resource options %u"), Size, (uint32)Options);
				
				if(bIsStatic)
				{
					[Backing.Buffer.GetPtr() setLabel:[NSString stringWithFormat:@"Static on frame %u", GetAGXDeviceContext().GetFrameNumberRHIThread()]];
				}
				else
				{
					[Backing.Buffer.GetPtr() setLabel:[NSString stringWithFormat:@"buffer on frame %u", GetAGXDeviceContext().GetFrameNumberRHIThread()]];
				}
				
#endif
			}
			
			for(FAGXBufferAndViews& Backing : BufferPool)
			{
				check(Backing.Buffer);
				check(AllocSize <= Backing.Buffer.GetLength());
				check([Backing.Buffer.GetPtr() storageMode] == StorageMode);
				check(Backing.Views.Num() == 0);
			}
		}
	}
}

FAGXRHIBuffer::~FAGXRHIBuffer()
{
	if(TransferBuffer)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, TransferBuffer.GetLength(), Usage);
		AGXSafeReleaseMetalBuffer(TransferBuffer);
	}
	
	for(FAGXBufferAndViews& Backing : BufferPool)
	{
		check(Backing.Buffer);
		
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Backing.Buffer.GetLength(), Usage);
		AGXSafeReleaseMetalBuffer(Backing.Buffer);
		
		for (auto& Pair : Backing.Views)
		{
			AGXSafeReleaseMetalTexture(Pair.Value);
			Pair.Value = nil;
		}
		Backing.Views.Empty();
	}

	if (Data)
	{
		METAL_INC_DWORD_STAT_BY(Type, MemFreed, Size, Usage);
		AGXSafeReleaseMetalObject(Data);
	}
}

void FAGXRHIBuffer::AllocTransferBuffer(bool bOnRHIThread, uint32 InSize, EResourceLockMode LockMode)
{
	check(!TransferBuffer);
	FAGXPooledBufferArgs ArgsCPU(InSize, BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions);
	TransferBuffer = GetAGXDeviceContext().CreatePooledBuffer(ArgsCPU);
	TransferBuffer.SetOwner(nullptr, false);
	check(TransferBuffer && TransferBuffer.GetPtr());
	METAL_INC_DWORD_STAT_BY(Type, MemAlloc, InSize, Usage);
	METAL_FATAL_ASSERT(TransferBuffer, TEXT("Failed to create buffer of size %u and storage mode %u"), InSize, (uint32)MTLStorageModeShared);
}

void FAGXRHIBuffer::AllocLinearTextures(const LinearTextureMapKey& InLinearTextureMapKey)
{
	check(MetalIsSafeToUseRHIThreadResources());
	
	const bool bWantsView = EnumHasAnyFlags(Usage, BUF_ShaderResource | BUF_UnorderedAccess);
	check(bWantsView);
	{
		FAGXBufferAndViews& CurrentBacking = GetCurrentBackingInternal();
		FAGXBuffer& CurrentBuffer = CurrentBacking.Buffer;
		
		check(CurrentBuffer);
		uint32 Length = CurrentBuffer.GetLength();
		uint8 InFormat = InLinearTextureMapKey.Key;
		const FAGXLinearTextureDescriptor& LinearTextureDesc = InLinearTextureMapKey.Value;
		
		MTLPixelFormat MTLFormat = (MTLPixelFormat)GAGXBufferFormats[InFormat].LinearTextureFormat;
		
		MTLTextureDescriptor* Desc = nil;
		MTLTextureUsage TexUsage = MTLTextureUsageUnknown;
		if (EnumHasAnyFlags(Usage, BUF_ShaderResource))
		{
			TexUsage |= MTLTextureUsageShaderRead;
		}
		if (EnumHasAnyFlags(Usage, BUF_UnorderedAccess))
		{
			TexUsage |= MTLTextureUsageShaderWrite;
		}
		
		uint32 BytesPerElement = (0 == LinearTextureDesc.BytesPerElement) ? GPixelFormats[InFormat].BlockBytes : LinearTextureDesc.BytesPerElement;
		if (MTLFormat == MTLPixelFormatRG11B10Float && MTLFormat != (MTLPixelFormat)GPixelFormats[InFormat].PlatformFormat)
		{
			BytesPerElement = 4;
		}

		const uint32 MinimumByteAlignment = GMtlppDevice.GetMinimumLinearTextureAlignmentForPixelFormat((mtlpp::PixelFormat)GAGXBufferFormats[InFormat].LinearTextureFormat);
		const uint32 MinimumElementAlignment = MinimumByteAlignment / BytesPerElement;

		uint32 Offset = LinearTextureDesc.StartOffsetBytes;
		check(Offset % MinimumByteAlignment == 0);

		uint32 NumElements = (UINT_MAX == LinearTextureDesc.NumElements) ? ((Size - Offset) / BytesPerElement) : LinearTextureDesc.NumElements;
		NumElements = Align(NumElements, MinimumElementAlignment);

		uint32 RowBytes = NumElements * BytesPerElement;

		{
			MTLResourceOptions Options = MTLResourceCPUCacheModeDefaultCache | (StorageMode << MTLResourceStorageModeShift) | MTLResourceHazardTrackingModeDefault;
			Desc = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:MTLFormat
																		  width:NumElements
																resourceOptions:Options
																		  usage:TexUsage];
			Desc.allowGPUOptimizedContents = NO;
		}

		for(FAGXBufferAndViews& Backing : BufferPool)
		{
			FAGXBuffer& Buffer = Backing.Buffer;
			FAGXTexture NewTexture([Buffer.GetPtr() newTextureWithDescriptor:Desc
																	  offset:(Offset + Buffer.GetOffset())
																 bytesPerRow:RowBytes], ns::Ownership::Assign);
			METAL_FATAL_ASSERT(NewTexture, TEXT("Failed to create linear texture, desc %s from buffer %s"), *FString([Desc description]), *FString([Buffer.GetPtr() description]));
			
			check(GAGXBufferFormats[InFormat].LinearTextureFormat == MTLPixelFormatRG11B10Float || GAGXBufferFormats[InFormat].LinearTextureFormat == (MTLPixelFormat)NewTexture.GetPixelFormat());
			Backing.Views.Add(InLinearTextureMapKey, NewTexture);
		}
	}
	
	for(FAGXBufferAndViews& Backing : BufferPool)
	{
		LinearTextureMap& Views = Backing.Views;
		check(Views.Find(InLinearTextureMapKey) != nullptr);
	}
}

struct FAGXRHICommandCreateLinearTexture : public FRHICommand<FAGXRHICommandCreateLinearTexture>
{
	FAGXRHIBuffer* Buffer;
	TRefCountPtr<FRHIResource> Parent;
	EPixelFormat Format;
	FAGXLinearTextureDescriptor LinearTextureDesc;
	
	FORCEINLINE_DEBUGGABLE FAGXRHICommandCreateLinearTexture(FAGXRHIBuffer* InBuffer, FRHIResource* InParent, EPixelFormat InFormat, const FAGXLinearTextureDescriptor* InLinearTextureDescriptor)
		: Buffer(InBuffer)
		, Parent(InParent)
		, Format(InFormat)
		, LinearTextureDesc()
	{
		if (InLinearTextureDescriptor)
		{
			LinearTextureDesc = *InLinearTextureDescriptor;
		}
	}
	
	virtual ~FAGXRHICommandCreateLinearTexture()
	{
	}
	
	void Execute(FRHICommandListBase& CmdList)
	{
		check(MetalIsSafeToUseRHIThreadResources());
		Buffer->CreateLinearTexture(Format, Parent.GetReference(), &LinearTextureDesc);
	}
};

void FAGXRHIBuffer::CreateLinearTexture(EPixelFormat InFormat, FRHIResource* InParent, const FAGXLinearTextureDescriptor* InLinearTextureDescriptor)
{
	SCOPED_AUTORELEASE_POOL;

	if (EnumHasAnyFlags(Usage, BUF_UnorderedAccess|BUF_ShaderResource) && GAGXBufferFormats[InFormat].LinearTextureFormat != MTLPixelFormatInvalid)
	{
		if (IsRunningRHIInSeparateThread() && !IsInRHIThread() && !FRHICommandListExecutor::GetImmediateCommandList().Bypass())
		{
			new (FRHICommandListExecutor::GetImmediateCommandList().AllocCommand<FAGXRHICommandCreateLinearTexture>()) FAGXRHICommandCreateLinearTexture(this, InParent, InFormat, InLinearTextureDescriptor);
		}
		else
		{
			check(MetalIsSafeToUseRHIThreadResources());
			LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FAGXLinearTextureDescriptor());
			
			FAGXBufferAndViews& Backing = GetCurrentBackingInternal();

			FAGXTexture* ExistingTexture = Backing.Views.Find(MapKey);
			if (!ExistingTexture)
			{
				AllocLinearTextures(MapKey);
			}
		}
	}
	
}

ns::AutoReleased<FAGXTexture> FAGXRHIBuffer::GetLinearTexture(EPixelFormat InFormat, const FAGXLinearTextureDescriptor* InLinearTextureDescriptor)
{
	ns::AutoReleased<FAGXTexture> Texture;
	if (EnumHasAnyFlags(Usage, BUF_UnorderedAccess|BUF_ShaderResource) && GAGXBufferFormats[InFormat].LinearTextureFormat != MTLPixelFormatInvalid)
	{
		LinearTextureMapKey MapKey = (InLinearTextureDescriptor != nullptr) ? LinearTextureMapKey(InFormat, *InLinearTextureDescriptor) : LinearTextureMapKey(InFormat, FAGXLinearTextureDescriptor());

		FAGXBufferAndViews& Backing = GetCurrentBackingInternal();
		FAGXTexture* ExistingTexture = Backing.Views.Find(MapKey);
		if (ExistingTexture)
		{
			Texture = *ExistingTexture;
		}
	}
	return Texture;
}

void* FAGXRHIBuffer::Lock(bool bIsOnRHIThread, EResourceLockMode InLockMode, uint32 Offset, uint32 InSize)
{
	check(CurrentLockMode == RLM_Num);
	check(LockSize == 0 && LockOffset == 0);
	check(MetalIsSafeToUseRHIThreadResources());
	check(!TransferBuffer);
	
	if (Data)
	{
		check(Data->Data);
		return ((uint8*)Data->Data) + Offset;
	}
	
	// The system is very naughty and does not obey this rule
//	check(LastLockFrame == 0 || LastLockFrame != GetAGXDeviceContext().GetFrameNumberRHIThread());
	
	const bool bWriteLock = InLockMode == RLM_WriteOnly;
	const bool bIsStatic = EnumHasAnyFlags(Usage, BUF_Static);
	const bool bIsDynamic = EnumHasAnyFlags(Usage, BUF_Dynamic);
	const bool bIsVolatile = EnumHasAnyFlags(Usage, BUF_Volatile);
	
	void* ReturnPointer = nullptr;
	
	uint32 Len = GetCurrentBacking().Buffer.GetLength(); // all buffers should have the same length or we are in trouble.
	check(Len >= InSize);
	
	if(bWriteLock)
	{
		if(bIsStatic)
		{
			// Static buffers do not discard. They just return the buffer or a transfer buffer.
			// You are not supposed to lock more than once a frame.
		}
		else
		{
			check(bIsDynamic || bIsVolatile);
			// cycle to next allocation
			AdvanceBackingIndex();
		}

		// Use transfer buffer for writing into 'Static' buffers as they could be in use by GPU atm
		// Initialization of 'Static' buffers still uses direct copy when possible
		const bool bUseTransferBuffer = (StorageMode == MTLStorageModePrivate || (StorageMode == MTLStorageModeShared && bIsStatic));
		if(bUseTransferBuffer)
		{
			FAGXFrameAllocator::AllocationEntry TempBacking = GetAGXDeviceContext().GetTransferAllocator()->AcquireSpace(Len);
			GetAGXDeviceContext().NewLock(this, TempBacking);
			check(TempBacking.Backing);
			ReturnPointer = (uint8*) [TempBacking.Backing contents] + TempBacking.Offset;
		}
		else
		{
			check(GetCurrentBacking().Buffer);
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
		check(ReturnPointer != nullptr);
	}
	else
	{
		check(InLockMode == EResourceLockMode::RLM_ReadOnly);
		// assumes offset is 0 for reads.
		check(Offset == 0);
		
		if (StorageMode == MTLStorageModePrivate)
		{
			SCOPE_CYCLE_COUNTER(STAT_AGXBufferPageOffTime);
			AllocTransferBuffer(true, Len, RLM_WriteOnly);
			check(TransferBuffer.GetLength() >= InSize);
			
			// Synchronise the buffer with the CPU
			GetAGXDeviceContext().CopyFromBufferToBuffer(GetCurrentBacking().Buffer, 0, TransferBuffer, 0, GetCurrentBacking().Buffer.GetLength());
			
			//kick the current command buffer.
			GetAGXDeviceContext().SubmitCommandBufferAndWait();
			
			ReturnPointer = TransferBuffer.GetContents();
		}
#if PLATFORM_MAC
		else if (StorageMode == MTLStorageModeManaged)
		{
			SCOPE_CYCLE_COUNTER(STAT_AGXBufferPageOffTime);
			
			// Synchronise the buffer with the CPU
			GetAGXDeviceContext().SynchronizeResource(GetCurrentBacking().Buffer.GetPtr());
			
			//kick the current command buffer.
			GetAGXDeviceContext().SubmitCommandBufferAndWait();
			
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
#endif
		else
		{
			// Shared
			ReturnPointer = GetCurrentBackingInternal().Buffer.GetContents();
		}
	} // Read Path
	
	
	
	check(GetCurrentBacking().Buffer);
	check(!GetCurrentBacking().Buffer.IsAliasable());
	
	check(ReturnPointer);
	LockOffset = Offset;
	LockSize = InSize;
	CurrentLockMode = InLockMode;
	
	if(InSize == 0)
	{
		LockSize = Len;
	}
	
	ReturnPointer = ((uint8*) (ReturnPointer)) + Offset;
	return ReturnPointer;
}

void FAGXRHIBuffer::Unlock()
{
	check(MetalIsSafeToUseRHIThreadResources());

	if (!Data)
	{
		FAGXBufferAndViews& Backing = GetCurrentBackingInternal();
		FAGXBuffer& CurrentBuffer = Backing.Buffer;
		
		check(CurrentBuffer);
		check(LockSize > 0);
		const bool bWriteLock = CurrentLockMode == RLM_WriteOnly;
		const bool bIsStatic = EnumHasAnyFlags(Usage, BUF_Static);
		
		if (bWriteLock)
		{
			check(!TransferBuffer);
			check(LockOffset == 0);
			check(LockSize <= CurrentBuffer.GetLength());
			
			// Use transfer buffer for writing into 'Static' buffers as they could be in use by GPU atm
			// Initialization of 'Static' buffers still uses direct copy when possible
			const bool bUseTransferBuffer = (StorageMode == MTLStorageModePrivate || (StorageMode == MTLStorageModeShared && bIsStatic));
			if (bUseTransferBuffer)
			{
				FAGXFrameAllocator::AllocationEntry Entry = GetAGXDeviceContext().FetchAndRemoveLock(this);
				FAGXBuffer Transfer = Entry.Backing;
				GetAGXDeviceContext().AsyncCopyFromBufferToBuffer(Transfer, Entry.Offset, CurrentBuffer, 0, LockSize);
			}
#if PLATFORM_MAC
			else if (StorageMode == MTLStorageModeManaged)
			{
				if (GAGXBufferZeroFill)
					MTLPP_VALIDATE(mtlpp::Buffer, CurrentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, DidModify(ns::Range(0, CurrentBuffer.GetLength())));
				else
					MTLPP_VALIDATE(mtlpp::Buffer, CurrentBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, DidModify(ns::Range(LockOffset, LockSize)));
			}
#endif //PLATFORM_MAC
			else
			{
				// shared buffers are always mapped so nothing happens
				check(StorageMode == MTLStorageModeShared);
			}
		}
		else
		{
			check(CurrentLockMode == RLM_ReadOnly);
			if (TransferBuffer)
			{
				check(StorageMode == MTLStorageModePrivate);
				AGXSafeReleaseMetalBuffer(TransferBuffer);
				TransferBuffer = nil;
			}
		}
	}
	
	check(!TransferBuffer);
	CurrentLockMode = RLM_Num;
	LockSize = 0;
	LockOffset = 0;
	LastLockFrame = GetAGXDeviceContext().GetFrameNumberRHIThread();
}

FAGXResourceMultiBuffer::FAGXResourceMultiBuffer(uint32 InSize, EBufferUsageFlags InUsage, EAGXBufferUsage InAgxUsage, uint32 InStride, ERHIResourceType Type)
	: FRHIBuffer(InSize, InUsage, InStride)
	, FAGXRHIBuffer(InSize, InUsage, InAgxUsage, Type)
	, IndexType((InStride == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
{
}

FAGXResourceMultiBuffer::~FAGXResourceMultiBuffer()
{
}

void FAGXResourceMultiBuffer::Swap(FAGXResourceMultiBuffer& Other)
{
	@autoreleasepool {
		FRHIBuffer::Swap(Other);
		FAGXRHIBuffer::Swap(Other);
		::Swap(IndexType, Other.IndexType);
	}
}

FBufferRHIRef FAGXDynamicRHI::RHICreateBuffer(FRHICommandListBase& RHICmdList, uint32 Size, EBufferUsageFlags Usage, uint32 Stride, ERHIAccess InResourceState, FRHIResourceCreateInfo& CreateInfo)
{
	@autoreleasepool{
	if (CreateInfo.bWithoutNativeResource)
	{
		return new FAGXResourceMultiBuffer(0, BUF_None, AGXBufferUsage(BUF_None), 0, RRT_Buffer);
	}

	// make the RHI object, which will allocate memory
	TRefCountPtr<FAGXResourceMultiBuffer> Buffer = new FAGXResourceMultiBuffer(Size, Usage, AGXBufferUsage(Usage), Stride, RRT_Buffer);
	Buffer->Init(RHICmdList, Size, Usage, CreateInfo, Buffer);
	return Buffer.GetReference();
	}
}

void* FAGXDynamicRHI::LockBuffer_BottomOfPipe(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI, uint32 Offset, uint32 Size, EResourceLockMode LockMode)
{
	@autoreleasepool {
	FAGXResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);

	// default to buffer memory
	return (uint8*)Buffer->Lock(true, LockMode, Offset, Size);
	}
}

void FAGXDynamicRHI::UnlockBuffer_BottomOfPipe(FRHICommandListBase& RHICmdList, FRHIBuffer* BufferRHI)
{
	@autoreleasepool {
	FAGXResourceMultiBuffer* Buffer = ResourceCast(BufferRHI);

	Buffer->Unlock();
	}
}

void FAGXDynamicRHI::RHICopyBuffer(FRHIBuffer* SourceBufferRHI, FRHIBuffer* DestBufferRHI)
{
	@autoreleasepool {
		FAGXResourceMultiBuffer* SrcBuffer = ResourceCast(SourceBufferRHI);
		FAGXResourceMultiBuffer* DstBuffer = ResourceCast(DestBufferRHI);
		
		const FAGXBuffer& TheSrcBuffer = SrcBuffer->GetCurrentBuffer();
		const FAGXBuffer& TheDstBuffer = DstBuffer->GetCurrentBuffer();
	
		if (TheSrcBuffer && TheDstBuffer)
		{
			GetAGXDeviceContext().CopyFromBufferToBuffer(TheSrcBuffer, 0, TheDstBuffer, 0, FMath::Min(SrcBuffer->GetSize(), DstBuffer->GetSize()));
		}
		else if (TheDstBuffer)
		{
			FAGXPooledBufferArgs ArgsCPU(SrcBuffer->GetSize(), BUF_Dynamic, FAGXPooledBufferArgs::SharedStorageResourceOptions);
			FAGXBuffer TempBuffer = GetAGXDeviceContext().CreatePooledBuffer(ArgsCPU);
			FMemory::Memcpy(TempBuffer.GetContents(), SrcBuffer->Data->Data, SrcBuffer->GetSize());
			GetAGXDeviceContext().CopyFromBufferToBuffer(TempBuffer, 0, TheDstBuffer, 0, FMath::Min(SrcBuffer->GetSize(), DstBuffer->GetSize()));
			AGXSafeReleaseMetalBuffer(TempBuffer);
		}
		else
		{
			void const* SrcData = SrcBuffer->Lock(true, RLM_ReadOnly, 0);
			void* DstData = DstBuffer->Lock(true, RLM_WriteOnly, 0);
			FMemory::Memcpy(DstData, SrcData, FMath::Min(SrcBuffer->GetSize(), DstBuffer->GetSize()));
			SrcBuffer->Unlock();
			DstBuffer->Unlock();
		}
	}
}

void FAGXDynamicRHI::RHITransferBufferUnderlyingResource(FRHIBuffer* DestBuffer, FRHIBuffer* SrcBuffer)
{
	@autoreleasepool {
		check(DestBuffer);
		FAGXResourceMultiBuffer* Dest = ResourceCast(DestBuffer);
		if (!SrcBuffer)
		{
			TRefCountPtr<FAGXResourceMultiBuffer> DeletionProxy = new FAGXResourceMultiBuffer(0, Dest->GetUsage(), Dest->GetAgxUsage(), Dest->GetStride(), Dest->Type);
			Dest->Swap(*DeletionProxy);
		}
		else
		{
			FAGXResourceMultiBuffer* Src = ResourceCast(SrcBuffer);
			Dest->Swap(*Src);
		}
	}
}

void FAGXRHIBuffer::Init(FRHICommandListBase& RHICmdList, uint32 InSize, EBufferUsageFlags InUsage, FRHIResourceCreateInfo& CreateInfo, FRHIResource* Resource)
{
	if (CreateInfo.ResourceArray)
	{
		check(InSize == CreateInfo.ResourceArray->GetResourceDataSize());
		
		if(Data)
		{
			FMemory::Memcpy(Data->Data, CreateInfo.ResourceArray->GetResourceData(), InSize);
		}
		else
		{
			if (StorageMode == MTLStorageModePrivate)
			{
				if (RHICmdList.IsBottomOfPipe())
				{
					void* Backing = this->Lock(true, RLM_WriteOnly, 0, InSize);
					FMemory::Memcpy(Backing, CreateInfo.ResourceArray->GetResourceData(), InSize);
					this->Unlock();
				}
				else
				{
					void* Result = FMemory::Malloc(InSize, 16);
					FMemory::Memcpy(Result, CreateInfo.ResourceArray->GetResourceData(), InSize);
					
					RHICmdList.EnqueueLambda(
						[this, Result, InSize](FRHICommandListBase& RHICmdList)
						{
							void* Backing = this->Lock(true, RLM_WriteOnly, 0, InSize);
							FMemory::Memcpy(Backing, Result, InSize);
							this->Unlock();
							FMemory::Free(Result);
						});
				}
			}
			else
			{
				FAGXBuffer& TheBuffer = GetCurrentBufferInternal();
				FMemory::Memcpy(TheBuffer.GetContents(), CreateInfo.ResourceArray->GetResourceData(), InSize);
	#if PLATFORM_MAC
				if (StorageMode == MTLStorageModeManaged)
				{
					MTLPP_VALIDATE(mtlpp::Buffer, TheBuffer, AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelValidation, DidModify(ns::Range(0, GAGXBufferZeroFill ? TheBuffer.GetLength() : InSize)));
				}
	#endif
			}
		}
		
		// Discard the resource array's contents.
		CreateInfo.ResourceArray->Discard();
	}
}