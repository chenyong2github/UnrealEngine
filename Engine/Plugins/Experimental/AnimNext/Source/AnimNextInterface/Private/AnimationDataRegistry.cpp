// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationDataRegistry.h"

#include "Misc/ScopeRWLock.h"
#include "AnimationDataRegistryTypes.h"
#include "AnimationGenerationTools.h"
#include "AnimNextInterfaceTypes.h"
#include "UObject/UObjectGlobals.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "BoneContainer.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Rendering/SkeletalMeshRenderData.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Rendering/SkeletalMeshRenderData.h"


namespace // Private
{

UE::AnimNext::Interface::FAnimationDataRegistry* GAnimationDataRegistry = nullptr;
constexpr int32 BASIC_TYPE_ALLOCK_BLOCK = 1000;
FDelegateHandle PostGarbageCollectHandle;

} // end namespace

namespace UE::AnimNext::Interface
{

/*static*/ void FAnimationDataRegistry::Init()
{
	if (GAnimationDataRegistry == nullptr)
	{
		GAnimationDataRegistry = new FAnimationDataRegistry();

		PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&FAnimationDataRegistry::HandlePostGarbageCollect);
	}
}

/*static*/ void FAnimationDataRegistry::Destroy()
{
	if (GAnimationDataRegistry != nullptr)
	{
		FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);

		GAnimationDataRegistry->ReleaseReferencePoseData(); // release any registered poses

		check(GAnimationDataRegistry->AllocatedBlocks.Num() == 0); // any other data should have been released at this point
		check(GAnimationDataRegistry->StoredData.Num() == 0);

		delete GAnimationDataRegistry;
		GAnimationDataRegistry = nullptr;
	}
}

FAnimationDataRegistry* FAnimationDataRegistry::Get()
{
	checkf(GAnimationDataRegistry, TEXT("Animation Data Registry is not instanced. It is only valid to access this while the engine module is loaded."));
	return GAnimationDataRegistry;
}


/*static*/ void FAnimationDataRegistry::HandlePostGarbageCollect()
{
	// Compact the registry on GC
	if (GAnimationDataRegistry)
	{
		FRWScopeLock Lock(GAnimationDataRegistry->SkeletalMeshReferencePosesLock, SLT_Write);

		for (auto Iter = GAnimationDataRegistry->SkeletalMeshReferencePoses.CreateIterator(); Iter; ++Iter)
		{
			const TWeakObjectPtr<const USkeletalMeshComponent>& SkeletalMeshComponentPtr = Iter.Key();
			if (SkeletalMeshComponentPtr.Get() == nullptr)
			{
				Iter.RemoveCurrent();
			}
		}
	}
}

FAnimationDataHandle FAnimationDataRegistry::RegisterReferencePose(USkeletalMeshComponent* SkeletalMeshComponent)
{
	FAnimationDataHandle Handle = AllocateData<FAnimationReferencePose>(1);

	FAnimationReferencePose& AnimationReferencePose = Handle.GetRef<FAnimationReferencePose>();

	FGenerationTools::GenerateReferencePose(SkeletalMeshComponent, SkeletalMeshComponent->GetSkeletalMeshAsset(), AnimationReferencePose);

	// register even if it fails to generate (register an empty ref pose)
	{
		FDelegateHandle DelegateHandle = SkeletalMeshComponent->RegisterOnLODRequiredBonesUpdate(FOnLODRequiredBonesUpdate::CreateRaw(this, &FAnimationDataRegistry::OnLODRequiredBonesUpdate));

		FRWScopeLock Lock(SkeletalMeshReferencePosesLock, SLT_Write);
		SkeletalMeshReferencePoses.Add(SkeletalMeshComponent, FReferencePoseData(Handle, DelegateHandle));
	}

	return Handle;
}

void FAnimationDataRegistry::OnLODRequiredBonesUpdate(USkeletalMeshComponent* SkeletalMeshComponent, int32 LODLevel, const TArray<FBoneIndexType>& LODRequiredBones)
{
	// TODO : Check if the LDO bomes are different from the currently calculated ReferencePose data (for now just delete the cached data)
	RemoveReferencePose(SkeletalMeshComponent);
}

FAnimationDataHandle FAnimationDataRegistry::GetOrGenerateReferencePose(USkeletalMeshComponent* SkeletalMeshComponent)
{
	FAnimationDataHandle ReturnHandle;

	{
		FRWScopeLock Lock(SkeletalMeshReferencePosesLock, SLT_ReadOnly);

		if (const FReferencePoseData* ReferencePoseData = SkeletalMeshReferencePoses.Find(SkeletalMeshComponent))
		{
			ReturnHandle = ReferencePoseData->AnimationDataHandle;
		}
	}
	
	if (ReturnHandle.IsValid() == false)
	{
		ReturnHandle = RegisterReferencePose(SkeletalMeshComponent);
	}

	return ReturnHandle;
}

void FAnimationDataRegistry::RemoveReferencePose(USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (SkeletalMeshComponent != nullptr)
	{
		FRWScopeLock Lock(SkeletalMeshReferencePosesLock, SLT_Write);

		FReferencePoseData ReferencePoseData;
		if (SkeletalMeshReferencePoses.RemoveAndCopyValue(SkeletalMeshComponent, ReferencePoseData))
		{
			SkeletalMeshComponent->UnregisterOnLODRequiredBonesUpdate(ReferencePoseData.DelegateHandle);
		}
	}
}



void FAnimationDataRegistry::RegisterData(const FName& Id, const FAnimationDataHandle& AnimationDataHandle)
{
	FRWScopeLock Lock(StoredDataLock, SLT_Write);
	StoredData.Add(Id, AnimationDataHandle);
}

void FAnimationDataRegistry::UnregisterData(const FName& Id)
{
	FRWScopeLock Lock(StoredDataLock, SLT_Write);
	StoredData.Remove(Id);
}

FAnimationDataHandle FAnimationDataRegistry::GetRegisteredData(const FName& Id) const
{
	FAnimationDataHandle ReturnHandle;

	{
		FRWScopeLock Lock(StoredDataLock, SLT_ReadOnly);

		if (const FAnimationDataHandle* HandlePtr = StoredData.Find(Id))
		{
			ReturnHandle = *HandlePtr;
		}
	}

	return ReturnHandle;
}

void FAnimationDataRegistry::FreeAllocatedBlock(Private::FAllocatedBlock* AllocatedBlock)
{
	FRWScopeLock Lock(DataTypeDefsLock, SLT_Write);

	if (ensure(AllocatedBlock != nullptr && AllocatedBlocks.Find(AllocatedBlock)))
	{
		if (AllocatedBlock->Memory != nullptr)
		{
			void* Memory = AllocatedBlock->Memory;

			FDataTypeDef* TypeDef = DataTypeDefs.Find(AllocatedBlock->TypeId);
			if (ensure(TypeDef != nullptr))
			{
				TypeDef->DestrooyTypeFn((uint8*)AllocatedBlock->Memory, AllocatedBlock->NumElem);

				FMemory::Free(AllocatedBlock->Memory); // TODO : This should come from preallocated chunks, use malloc / free for now
				AllocatedBlock->Memory = nullptr;
			}

			delete AllocatedBlock; // TODO : avoid memory fragmentation
			AllocatedBlocks.Remove(AllocatedBlock);
		}
	}
}

// Remove any ReferencePoses and unregister all the SkeletalMeshComponent delegates (if any still alive)
void FAnimationDataRegistry::ReleaseReferencePoseData()
{
	FRWScopeLock Lock(SkeletalMeshReferencePosesLock, SLT_Write);

	for (auto Iter = GAnimationDataRegistry->SkeletalMeshReferencePoses.CreateIterator(); Iter; ++Iter)
	{
		const TWeakObjectPtr<USkeletalMeshComponent>& SkeletalMeshComponentPtr = Iter.Key();
		const FReferencePoseData& ReferencePoseData = Iter.Value();

		if (USkeletalMeshComponent* SkeletalMeshComponent = SkeletalMeshComponentPtr.Get())
		{
			SkeletalMeshComponent->UnregisterOnLODRequiredBonesUpdate(ReferencePoseData.DelegateHandle);
		}
	}

	SkeletalMeshReferencePoses.Empty();
}

} // end namespace UE::AnimNext::Interface
