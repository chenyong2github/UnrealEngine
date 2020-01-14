// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PhysXSupport.h: PhysX support
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "PhysicsCore.h"
#include "PhysicsPublicCore.h"

#if WITH_PHYSX
#include "PhysXPublicCore.h"
#include "HAL/IConsoleManager.h"

/////// GLOBAL POINTERS
/** Pointer to PhysX Foundation singleton */
extern PHYSICSCORE_API physx::PxFoundation* GPhysXFoundation;
/** Pointer to PhysX debugger */
extern PHYSICSCORE_API PxPvd* GPhysXVisualDebugger;

// Whether to track PhysX memory allocations
#ifndef PHYSX_MEMORY_VALIDATION
#define PHYSX_MEMORY_VALIDATION		0
#endif

// Whether to track PhysX memory allocations
#ifndef PHYSX_MEMORY_STATS
#define PHYSX_MEMORY_STATS		0 || PHYSX_MEMORY_VALIDATION
#endif

#define PHYSX_MEMORY_STAT_ONLY (0)

/** PhysX memory allocator wrapper */
class PHYSICSCORE_API FPhysXAllocator : public PxAllocatorCallback
{
#if PHYSX_MEMORY_STATS
	TMap<FName, size_t> AllocationsByType;

	struct FPhysXAllocationHeader
	{
		FPhysXAllocationHeader(FName InAllocationTypeName, size_t InAllocationSize)
			: AllocationTypeName(InAllocationTypeName)
			, AllocationSize(InAllocationSize)
		{
			static_assert((sizeof(FPhysXAllocationHeader) % 16) == 0, "FPhysXAllocationHeader size must multiple of bytes.");
			MagicPadding();
		}

		void MagicPadding()
		{
			for (uint8 ByteCount = 0; ByteCount < sizeof(Padding); ++ByteCount)
			{
				Padding[ByteCount] = 'A' + ByteCount % 4;
			}
		}

		bool operator==(const FPhysXAllocationHeader& OtherHeader) const
		{
			bool bHeaderSame = AllocationTypeName == OtherHeader.AllocationTypeName && AllocationSize == OtherHeader.AllocationSize;
			for (uint8 ByteCount = 0; ByteCount < sizeof(Padding); ++ByteCount)
			{
				bHeaderSame &= Padding[ByteCount] == OtherHeader.Padding[ByteCount];
			}

			return bHeaderSame;
		}

		FName AllocationTypeName;
		size_t	AllocationSize;
		static const int PaddingSize = 8;
		uint8 Padding[PaddingSize];	//physx needs 16 byte alignment. Additionally we fill padding with a pattern to see if there's any memory stomps
		uint8 Padding2[(sizeof(FName) + sizeof(size_t) + PaddingSize) % 16];

		void Validate() const
		{
			bool bValid = true;
			for (uint8 ByteCount = 0; ByteCount < sizeof(Padding); ++ByteCount)
			{
				bValid &= Padding[ByteCount] == 'A' + ByteCount % 4;
			}

			check(bValid);

			FPhysXAllocationHeader* AllocationFooter = (FPhysXAllocationHeader*)(((uint8*)this) + sizeof(FPhysXAllocationHeader) + AllocationSize);
			check(*AllocationFooter == *this);
		}
	};

#endif

public:

#if PHYSX_MEMORY_VALIDATION

	/** Iterates over all allocations and checks that they the headers and footers are valid */
	void ValidateHeaders()
	{
		check(IsInGameThread());
		FPhysXAllocationHeader* TmpHeader = nullptr;
		while (NewHeaders.Dequeue(TmpHeader))
		{
			AllocatedHeaders.Add(TmpHeader);
		}

		while (OldHeaders.Dequeue(TmpHeader))
		{
			AllocatedHeaders.Remove(TmpHeader);
		}

		FScopeLock Lock(&ValidationCS);	//this is needed in case another thread is freeing the header
		for (FPhysXAllocationHeader* Header : AllocatedHeaders)
		{
			Header->Validate();
		}
	}
#endif

	FPhysXAllocator()
	{}

	virtual ~FPhysXAllocator()
	{}

	virtual void* allocate(size_t size, const char* typeName, const char* filename, int line) override
	{
#if PHYSX_MEMORY_STATS
		INC_DWORD_STAT_BY(STAT_MemoryPhysXTotalAllocationSize, size);


		FString AllocationString = FString::Printf(TEXT("%s %s:%d"), ANSI_TO_TCHAR(typeName), ANSI_TO_TCHAR(filename), line);
		FName AllocationName(*AllocationString);

		// Assign header
		FPhysXAllocationHeader* AllocationHeader = (FPhysXAllocationHeader*)FMemory::Malloc(size + sizeof(FPhysXAllocationHeader) * 2, 16);
		AllocationHeader->AllocationTypeName = AllocationName;
		AllocationHeader->AllocationSize = size;
		AllocationHeader->MagicPadding();
		FPhysXAllocationHeader* AllocationFooter = (FPhysXAllocationHeader*)(((uint8*)AllocationHeader) + size + sizeof(FPhysXAllocationHeader));
		AllocationFooter->AllocationTypeName = AllocationName;
		AllocationFooter->AllocationSize = size;
		AllocationFooter->MagicPadding();

		size_t* TotalByType = AllocationsByType.Find(AllocationName);	//TODO: this is not thread safe!
		if (TotalByType)
		{
			*TotalByType += size;
		}
		else
		{
			AllocationsByType.Add(AllocationName, size);
		}

#if PHYSX_MEMORY_VALIDATION
		NewHeaders.Enqueue(AllocationHeader);
#endif

		return (uint8*)AllocationHeader + sizeof(FPhysXAllocationHeader);
#else
		LLM_SCOPE(ELLMTag::PhysXAllocator);
		void* ptr = FMemory::Malloc(size, 16);
#if PHYSX_MEMORY_STAT_ONLY
		INC_DWORD_STAT_BY(STAT_MemoryPhysXTotalAllocationSize, FMemory::GetAllocSize(ptr));
#endif
		return ptr;
#endif
	}

	virtual void deallocate(void* ptr) override
	{
#if PHYSX_MEMORY_STATS
		if (ptr)
		{
			FPhysXAllocationHeader* AllocationHeader = (FPhysXAllocationHeader*)((uint8*)ptr - sizeof(FPhysXAllocationHeader));
#if PHYSX_MEMORY_VALIDATION
			AllocationHeader->Validate();
			OldHeaders.Enqueue(AllocationHeader);
			FScopeLock Lock(&ValidationCS);	//this is needed in case we are in the middle of validating the headers
#endif

			DEC_DWORD_STAT_BY(STAT_MemoryPhysXTotalAllocationSize, AllocationHeader->AllocationSize);
			size_t* TotalByType = AllocationsByType.Find(AllocationHeader->AllocationTypeName);
			*TotalByType -= AllocationHeader->AllocationSize;
			FMemory::Free(AllocationHeader);
		}
#else
#if PHYSX_MEMORY_STAT_ONLY
		DEC_DWORD_STAT_BY(STAT_MemoryPhysXTotalAllocationSize, FMemory::GetAllocSize(ptr));
#endif
		FMemory::Free(ptr);
#endif
	}

#if PHYSX_MEMORY_STATS
	void DumpAllocations(FOutputDevice* Ar)
	{
		struct FSortBySize
		{
			FORCEINLINE bool operator()(const size_t& A, const size_t& B) const
			{
				// Sort descending
				return B < A;
			}
		};

		size_t TotalSize = 0;
		AllocationsByType.ValueSort(FSortBySize());
		for (auto It = AllocationsByType.CreateConstIterator(); It; ++It)
		{
			TotalSize += It.Value();
			Ar->Logf(TEXT("%-10d %s"), It.Value(), *It.Key().ToString());
		}

		Ar->Logf(TEXT("Total:%-10d"), TotalSize);
	}
#endif

#if PHYSX_MEMORY_VALIDATION
private:
	FCriticalSection ValidationCS;
	TSet<FPhysXAllocationHeader*> AllocatedHeaders;

	//Since this needs to be thread safe we can't add to the allocated headers set until we're on the game thread
	TQueue<FPhysXAllocationHeader*, EQueueMode::Mpsc> NewHeaders;
	TQueue<FPhysXAllocationHeader*, EQueueMode::Mpsc> OldHeaders;

#endif
};

extern PHYSICSCORE_API int32 GPhysXHackCurrentLoopCounter;

/** PhysX output stream wrapper */
class PHYSICSCORE_API FPhysXErrorCallback : public PxErrorCallback
{
public:
	virtual void reportError(PxErrorCode::Enum e, const char* message, const char* file, int line) override;
};

extern PHYSICSCORE_API TAutoConsoleVariable<float> CVarToleranceScaleLength;
extern PHYSICSCORE_API TAutoConsoleVariable<float> CVarToleranceScaleSpeed;


/** Utility class to keep track of shared physics data */
class PHYSICSCORE_API FPhysxSharedData
{
public:
	static FPhysxSharedData& Get() { return *Singleton; }
	static void Initialize();
	static void Terminate();

	static void LockAccess();
	static void UnlockAccess();

	void Add(PxBase* Obj, const FString& OwnerName);
	void Remove(PxBase* Obj);

	const PxCollection* GetCollection() { return SharedObjects; }

	void DumpSharedMemoryUsage(FOutputDevice* Ar);
private:
	/** Collection of shared physx objects */
	PxCollection* SharedObjects;
	TMap<PxBase*, FString> OwnerNames;
	FCriticalSection CriticalSection;

	static FPhysxSharedData* Singleton;

	FPhysxSharedData()
	{
		SharedObjects = PxCreateCollection();
	}

	~FPhysxSharedData()
	{
		SharedObjects->release();
	}

};


/** Utility wrapper for a PhysX output stream that only counts the memory. */
class PHYSICSCORE_API FPhysXCountMemoryStream : public PxOutputStream
{
public:
	/** Memory used by the serialized object(s) */
	uint32 UsedMemory;

	FPhysXCountMemoryStream()
		: UsedMemory(0)
	{}

	virtual PxU32 write(const void* Src, PxU32 Count) override
	{
		UsedMemory += Count;
		return Count;
	}
};

/** Utility wrapper for a uint8 TArray for saving into PhysX. */
class PHYSICSCORE_API FPhysXOutputStream : public PxOutputStream
{
public:
	/** Raw byte data */
	TArray<uint8>			*Data;

	FPhysXOutputStream()
		: Data(NULL)
	{}

	FPhysXOutputStream(TArray<uint8> *InData)
		: Data(InData)
	{}

	PxU32 write(const void* Src, PxU32 Count)
	{
		check(Data);
		if (Count)	//PhysX serializer can pass us 0 bytes to write
		{
			check(Src);
			int32 CurrentNum = (*Data).Num();
			(*Data).AddUninitialized(Count);
			FMemory::Memcpy(&(*Data)[CurrentNum], Src, Count);
		}

		return Count;
	}
};

/**
 * Returns the in-memory size of the specified object by serializing it.
 *
 * @param	Obj					Object to determine the memory footprint for
 * @param	SharedCollection	Shared collection of data to ignore
 * @returns						Size of the object in bytes determined by serialization
 **/
PHYSICSCORE_API SIZE_T GetPhysxObjectSize(PxBase* Obj, const PxCollection* SharedCollection);

PHYSICSCORE_API void PvdConnect(FString Host, bool bVisualization);


#if WITH_APEX
/**
	"Null" render resource manager callback for APEX
	This just gives a trivial implementation of the interface, since we are not using the APEX rendering API
*/
class PHYSICSCORE_API FApexNullRenderResourceManager : public nvidia::apex::UserRenderResourceManager
{
public:
	// NxUserRenderResourceManager interface.

	virtual nvidia::apex::UserRenderVertexBuffer*	createVertexBuffer(const nvidia::apex::UserRenderVertexBufferDesc&) override
	{
		return NULL;
	}
	virtual nvidia::apex::UserRenderIndexBuffer*	createIndexBuffer(const nvidia::apex::UserRenderIndexBufferDesc&) override
	{
		return NULL;
	}
	virtual nvidia::apex::UserRenderBoneBuffer*		createBoneBuffer(const nvidia::apex::UserRenderBoneBufferDesc&) override
	{
		return NULL;
	}
	virtual nvidia::apex::UserRenderInstanceBuffer*	createInstanceBuffer(const nvidia::apex::UserRenderInstanceBufferDesc&) override
	{
		return NULL;
	}
	virtual nvidia::apex::UserRenderSpriteBuffer*   createSpriteBuffer(const nvidia::apex::UserRenderSpriteBufferDesc&) override
	{
		return NULL;
	}

	virtual nvidia::apex::UserRenderSurfaceBuffer*  createSurfaceBuffer(const nvidia::apex::UserRenderSurfaceBufferDesc& desc)   override
	{
		return NULL;
	}

	virtual nvidia::apex::UserRenderResource*		createResource(const nvidia::apex::UserRenderResourceDesc&) override
	{
		return NULL;
	}
	virtual void						releaseVertexBuffer(nvidia::apex::UserRenderVertexBuffer&) override {}
	virtual void						releaseIndexBuffer(nvidia::apex::UserRenderIndexBuffer&) override {}
	virtual void						releaseBoneBuffer(nvidia::apex::UserRenderBoneBuffer&) override {}
	virtual void						releaseInstanceBuffer(nvidia::apex::UserRenderInstanceBuffer&) override {}
	virtual void						releaseSpriteBuffer(nvidia::apex::UserRenderSpriteBuffer&) override {}
	virtual void                        releaseSurfaceBuffer(nvidia::apex::UserRenderSurfaceBuffer& buffer) override {}
	virtual void						releaseResource(nvidia::apex::UserRenderResource&) override {}

	virtual physx::PxU32				getMaxBonesForMaterial(void*) override
	{
		return 0;
	}
	virtual bool						getSpriteLayoutData(physx::PxU32, physx::PxU32, nvidia::apex::UserRenderSpriteBufferDesc*) override
	{
		return false;
	}
	virtual bool						getInstanceLayoutData(physx::PxU32, physx::PxU32, nvidia::apex::UserRenderInstanceBufferDesc*) override
	{
		return false;
	}

};
extern PHYSICSCORE_API FApexNullRenderResourceManager GApexNullRenderResourceManager;

/**
	APEX resource callback
	The resource callback is how APEX asks the application to find assets when it needs them
*/
class PHYSICSCORE_API FApexResourceCallback : public nvidia::apex::ResourceCallback
{
public:
	// NxResourceCallback interface.

	virtual void* requestResource(const char* NameSpace, const char* Name) override
	{
		// Here a pointer is looked up by name and returned
		(void)NameSpace;
		(void)Name;

		return NULL;
	}

	virtual void  releaseResource(const char* NameSpace, const char* Name, void* Resource) override
	{
		// Here we release a named resource
		(void)NameSpace;
		(void)Name;
		(void)Resource;
	}
};
extern PHYSICSCORE_API FApexResourceCallback GApexResourceCallback;


#endif // #if WITH_APEX

inline PxSceneDesc CreateDummyPhysXSceneDescriptor()
{
	PxSceneDesc Desc(GPhysXSDK->getTolerancesScale());
	Desc.filterShader = PxDefaultSimulationFilterShader;
	Desc.cpuDispatcher = PxDefaultCpuDispatcherCreate(4);
	return Desc;
}

#endif