// Copyright Epic Games, Inc. All Rights Reserved.

#include "BulkDataRegistryEditorDomain.h"

#include "Serialization/BulkDataRegistry.h"
#include "Compression/CompressedBuffer.h"
#include "DerivedDataCacheRecord.h"
#include "EditorBuildInputResolver.h"
#include "EditorDomain/EditorDomainUtils.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "IO/IoHash.h"
#include "Memory/SharedBuffer.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Serialization/MemoryReader.h"
#include "Templates/RefCounting.h"
#include "TickableEditorObject.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace UE::BulkDataRegistry::Private
{

namespace Constants
{
	constexpr double UpdateHashesInitialWaitDuration = 1.0;
	constexpr uint64 TempLoadedPayloadsSizeBudget = 1024 * 1024 * 100;
	constexpr double TempLoadedPayloadsDuration = 60.;
	constexpr double PendingPackageCooldown = 60.;

}

/** Add a hook to the BulkDataRegistry's startup delegate to use the EditorDomain as the BulkDataRegistry */
class FEditorDomainRegisterAsBulkDataRegistry
{
public:
	FEditorDomainRegisterAsBulkDataRegistry()
	{
		IBulkDataRegistry::GetSetBulkDataRegistryDelegate().BindStatic(SetBulkDataRegistry);
	}

	static IBulkDataRegistry* SetBulkDataRegistry()
	{
		return new FBulkDataRegistryEditorDomain();
	}
} GRegisterAsBulkDataRegistry;


FBulkDataRegistryEditorDomain::FBulkDataRegistryEditorDomain()
{
	SharedDataLock = new FTaskSharedDataLock();
	// We piggyback on the BulkDataRegistry hook to set the pointer to tunnel in the pointer to the EditorBuildInputResolver as well
	SetGlobalBuildInputResolver(&UE::DerivedData::FEditorBuildInputResolver::Get());
	FCoreUObjectDelegates::OnEndLoadPackage.AddRaw(this, &FBulkDataRegistryEditorDomain::OnEndLoadPackage);
}

FBulkDataRegistryEditorDomain::~FBulkDataRegistryEditorDomain()
{
	FCoreUObjectDelegates::OnEndLoadPackage.RemoveAll(this);
	SetGlobalBuildInputResolver(nullptr);

	TMap<FGuid, FUpdatingPayload> LocalUpdatingPayloads;
	{
		FWriteScopeLock SharedDataScopeLock(SharedDataLock->ActiveLock);
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);

		// Disable all activity that might come in from other threads
		bActive = false;
		SharedDataLock->bActive = false;

		// Take custody of UpdatingPayloads
		Swap(LocalUpdatingPayloads, UpdatingPayloads);
	}

	// Since the AsyncTasks can no longer call the Requesters, we have to do it
	for (TPair<FGuid, FUpdatingPayload>& Pair : LocalUpdatingPayloads)
	{
		for (TUniqueFunction<void(bool, const FCompressedBuffer&)>& Requester : Pair.Value.Requesters)
		{
			Requester(false, FCompressedBuffer());
		}
	}
	LocalUpdatingPayloads.Empty();

	// Clear PendingPackages
	{
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);
		PendingPackages.Empty();
	}
}

void FBulkDataRegistryEditorDomain::Register(UPackage* Owner, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
{
	if (!BulkData.GetIdentifier().IsValid())
	{
		return;
	}

	FName PackageName = NAME_None;
	UE::Virtualization::FVirtualizedUntypedBulkData CopyBulk(BulkData.CopyTornOff());
	if (Owner
		&& Owner->GetFileSize() // We only record the BulkDataList for disk packages
		&& CopyBulk.CanSaveForRegistry()
		)
	{
		PackageName = Owner->GetFName();
		AddPendingPackageBulkData(PackageName, CopyBulk);
	}

	FName PackageNameToUpdate = CopyBulk.HasPlaceholderPayloadId() ? PackageName : NAME_None;
	FWriteScopeLock RegistryScopeLock(RegistryLock);
	check(bActive); // Registrations should not come in after we destruct
	Registry.Add(BulkData.GetIdentifier(), FRegisteredBulk(MoveTemp(CopyBulk), PackageNameToUpdate));
}

void FBulkDataRegistryEditorDomain::OnEndLoadPackage(TConstArrayView<UPackage*> LoadedPackages)
{
}

void FBulkDataRegistryEditorDomain::OnExitMemory(const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
{
	const FGuid& Key = BulkData.GetIdentifier();
	FWriteScopeLock RegistryScopeLock(RegistryLock);
	check(bActive); // Deregistrations should not come in after we destruct
	FRegisteredBulk* Existing = Registry.Find(Key);
	if (Existing)
	{
		if (Existing->BulkData.IsMemoryOnlyPayload())
		{
			Registry.Remove(Key);
		}
	}
}

TFuture<UE::BulkDataRegistry::FMetaData> FBulkDataRegistryEditorDomain::GetMeta(const FGuid& BulkDataId)
{
	bool bIsWriteLock = false;
	FRWScopeLock RegistryScopeLock(RegistryLock, SLT_ReadOnly);
	for (;;)
	{
		FRegisteredBulk* Existing = nullptr;
		if (bActive)
		{
			Existing = Registry.Find(BulkDataId);
		}
		if (!Existing)
		{
			TPromise<UE::BulkDataRegistry::FMetaData> Promise;
			Promise.SetValue(UE::BulkDataRegistry::FMetaData{ false, FIoHash(), 0 });
			return Promise.GetFuture();
		}

		UE::Virtualization::FVirtualizedUntypedBulkData& BulkData = Existing->BulkData;
		if (!BulkData.HasPlaceholderPayloadId())
		{
			TPromise<UE::BulkDataRegistry::FMetaData> Promise;
			Promise.SetValue(UE::BulkDataRegistry::FMetaData{ true, BulkData.GetPayloadId().GetIdentifier(), static_cast<uint64>(BulkData.GetPayloadSize()) });
			return Promise.GetFuture();
		}

		if (!bIsWriteLock)
		{
			bIsWriteLock = true;
			RegistryScopeLock.ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
			continue;
		}

		// The payload in the registry is missing its RawHash; start a thread to calculate it and subscribe our caller to the results
		FUpdatingPayload& UpdatingPayload = UpdatingPayloads.FindOrAdd(BulkDataId);
		if (!UpdatingPayload.AsyncTask)
		{
			UpdatingPayload.AsyncTask = new FAutoDeleteAsyncTask<FUpdatePayloadWorker>(this, BulkData);
			UpdatingPayload.AsyncTask->StartBackgroundTask();
		}
		TPromise<UE::BulkDataRegistry::FMetaData> Promise;
		TFuture<UE::BulkDataRegistry::FMetaData> Future = Promise.GetFuture();
		UpdatingPayload.Requesters.Add([Promise = MoveTemp(Promise)](bool bValid, const FCompressedBuffer& Buffer) mutable
			{
				Promise.SetValue(UE::BulkDataRegistry::FMetaData{ bValid, Buffer.GetRawHash(), Buffer.GetRawSize() });
			});
		return Future;
	}
}

TFuture<UE::BulkDataRegistry::FData> FBulkDataRegistryEditorDomain::GetData(const FGuid& BulkDataId)
{
	TOptional<UE::Virtualization::FVirtualizedUntypedBulkData> CopyBulk;
	{
		bool bIsWriteLock = false;
		FRWScopeLock RegistryScopeLock(RegistryLock, SLT_ReadOnly);
		for (;;)
		{
			FRegisteredBulk* Existing = nullptr;
			if (bActive)
			{
				Existing = Registry.Find(BulkDataId);
			}
			if (!Existing)
			{
				TPromise<UE::BulkDataRegistry::FData> Result;
				Result.SetValue(UE::BulkDataRegistry::FData{ false, FCompressedBuffer() });
				return Result.GetFuture();
			}

			if (!Existing->BulkData.HasPlaceholderPayloadId() && !Existing->bHasTempPayload)
			{
				// The contract of FVirtualizedUntypedBulkData does not guarantee that GetCompressedPayload() is a quick operation (it may load the data
				// synchronously), so copy the BulkData into a temporary and call it outside the lock
				CopyBulk.Emplace(Existing->BulkData);
				break;
			}

			if (!bIsWriteLock)
			{
				bIsWriteLock = true;
				RegistryScopeLock.ReleaseReadOnlyLockAndAcquireWriteLock_USE_WITH_CAUTION();
				continue;
			}

			if (!Existing->BulkData.HasPlaceholderPayloadId())
			{
				check(Existing->bHasTempPayload);
				// We are the first GetData call after the BulkData previously loaded its Payload to calculate the RawHash.
				// Sidenote, this means GetCompressedPayload will be fast.
				// But we also have the responsibility to dump the data from memory since we have now consumed it.
				// Make sure we copy the data pointer before dumping it from the Registry version!
				CopyBulk.Emplace(Existing->BulkData);
				Existing->BulkData.UnloadData();
				Existing->bHasTempPayload = false;
				break;
			}

			// The payload in the registry is missing its RawHash, and we calculate that on demand whenever the data is requested,
			// which is now. Instead of only returning the data to our caller, we load the data and use it to update the RawHash
			// in the registry and then return the data to our caller.
			FUpdatingPayload& UpdatingPayload = UpdatingPayloads.FindOrAdd(BulkDataId);
			if (!UpdatingPayload.AsyncTask)
			{
				UpdatingPayload.AsyncTask = new FAutoDeleteAsyncTask<FUpdatePayloadWorker>(this, Existing->BulkData);
			}
			TPromise<UE::BulkDataRegistry::FData> Promise;
			TFuture<UE::BulkDataRegistry::FData> Future = Promise.GetFuture();
			UpdatingPayload.Requesters.Add([Promise=MoveTemp(Promise)](bool bValid, const FCompressedBuffer& Buffer) mutable
				{
					Promise.SetValue(UE::BulkDataRegistry::FData{ bValid, Buffer });
				});
			return Future;
		}
	}

	// We are calling a function that returns a TFuture on the stack-local CopyBulk, which would cause a read-after-free if the asynchronous TFuture could
	// read from the BulkData. However, the contract of FVirtualizedUntypedBulkData guarantees that the TFuture gets a copy of all data it needs and
	// does not read from the BulkData after returning from GetCompressedPayload, so a read-after-free is not possible.
	return CopyBulk->GetCompressedPayload().Next([](FCompressedBuffer Payload)
		{
			return UE::BulkDataRegistry::FData{ true, Payload };
		});
}

void FBulkDataRegistryEditorDomain::TickCook(float DeltaTime, bool bTickComplete)
{
	bool bWaitForCooldown = !bTickComplete;
	check(bActive); // Ticks should not come in after we destruct

	PollPendingPackages(bWaitForCooldown);
	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		PruneTempLoadedPayloads();
	}
}

void FBulkDataRegistryEditorDomain::Tick(float DeltaTime)
{
	TickCook(DeltaTime, false /* bTickComplete */);
}

void FBulkDataRegistryEditorDomain::AddPendingPackageBulkData(FName PackageName, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
{
	FScopeLock PendingPackageScopeLock(&PendingPackageLock);
	AddPendingPackageBulkDataInsideLock(PackageName, BulkData);
}

void FBulkDataRegistryEditorDomain::AddPendingPackageBulkDataInsideLock(FName PackageName, const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
{
	check(bActive); // Registrations should not come in after we destruct, and AsyncTasks should check bActive before calling
	TUniquePtr<FPendingPackage>& PendingPackage = PendingPackages.FindOrAdd(PackageName);
	if (!PendingPackage.IsValid())
	{
		PendingPackage = MakeUnique<FPendingPackage>(PackageName, this);
	}
	PendingPackage->AddBulkData(BulkData);
}

void FBulkDataRegistryEditorDomain::PollPendingPackages(bool bWaitForCooldown)
{
	// BULKDATAREGISTRY_TODO: Reduce array iteration time; tick is called frequently in the editor
	TArray<TPair<FName, TUniquePtr<FPendingPackage>>> PoppedPackages;
	{
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);
		if (PendingPackages.Num() == 0)
		{
			return;
		}

		double CreateTimeToPush = 0.0;
		if (bWaitForCooldown)
		{
			CreateTimeToPush = FPlatformTime::Seconds() - Constants::PendingPackageCooldown;
		}
		for (TPair<FName, TUniquePtr<FPendingPackage>>& Pair : PendingPackages)
		{
			FPendingPackage* PendingPackage = Pair.Value.Get();
			if (PendingPackage->IsReadFinished() && (!bWaitForCooldown || PendingPackage->GetCreateTime() <= CreateTimeToPush))
			{
				PoppedPackages.Emplace(Pair.Key, MoveTemp(Pair.Value));
			}
		}
		for (TPair<FName, TUniquePtr<FPendingPackage>>& Pair : PoppedPackages)
		{
			PendingPackages.Remove(Pair.Get<0>());
		}
	}

	TArray<UE::Virtualization::FVirtualizedUntypedBulkData> BulkDatas;
	for (TPair<FName, TUniquePtr<FPendingPackage>>& Pair : PoppedPackages)
	{
		FName PackageName = Pair.Get<0>();
		TUniquePtr<FPendingPackage>& PendingPackage = Pair.Get<1>();
		PendingPackage->UpdateCache();
	}
}

void FBulkDataRegistryEditorDomain::AddTempLoadedPayload(const FGuid& RegistryKey, uint64 PayloadSize)
{
	// Called within RegistryLock WriteLock
	TempLoadedPayloads.Add(FTempLoadedPayload{ RegistryKey, PayloadSize, FPlatformTime::Seconds() + Constants::TempLoadedPayloadsDuration });
	TempLoadedPayloadsSize += PayloadSize;
}

void FBulkDataRegistryEditorDomain::PruneTempLoadedPayloads()
{
	// Called within RegistryLock WriteLock
	if (!TempLoadedPayloads.IsEmpty())
	{
		double CurrentTime = FPlatformTime::Seconds();
		while (!TempLoadedPayloads.IsEmpty() &&
			(TempLoadedPayloadsSize > Constants::TempLoadedPayloadsSizeBudget
				|| TempLoadedPayloads[0].EndTime <= CurrentTime))
		{
			FTempLoadedPayload Payload = TempLoadedPayloads.PopFrontValue();
			FRegisteredBulk* Existing = Registry.Find(Payload.Guid);
			if (Existing)
			{
				// UnloadData only unloads the in-memory data, and only if the BulkData can be reloaded from disk
				Existing->BulkData.UnloadData();
				Existing->bHasTempPayload = false;
			}
			TempLoadedPayloadsSize -= Payload.PayloadSize;
		}
	}
}

TConstArrayView<uint8> MakeArrayView(FSharedBuffer Buffer)
{
	return TConstArrayView<uint8>(reinterpret_cast<const uint8*>(Buffer.GetData()), Buffer.GetSize());
}

FPendingPackage::FPendingPackage(FName InPackageName, FBulkDataRegistryEditorDomain* InOwner)
	: PackageName(InPackageName)
	, Owner(InOwner)
	, CreateTime(FPlatformTime::Seconds())
{
	CacheRequest = UE::EditorDomain::GetBulkDataList(PackageName, [this](FSharedBuffer Buffer)
		{ OnCacheResults(Buffer); });
}

FPendingPackage::~FPendingPackage()
{
	CacheRequest.Cancel();
}

void FPendingPackage::OnCacheResults(FSharedBuffer Buffer)
{
	if (Buffer.IsNull())
	{
		return;
	}
	FMemoryReaderView Reader(MakeArrayView(Buffer));
	Serialize(Reader, CachedBulkDatas);
	if (Reader.IsError())
	{
		CachedBulkDatas.Empty();
	}

	// Add each CachedBulkData to the Registry. If the CachedBulkData exists, do not overwrite it, but do update the RawHash if it is missing it.
	FWriteScopeLock RegistryScopeLock(Owner->RegistryLock);
	if (!Owner->bActive)
	{
		return;
	}
	for (const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData : CachedBulkDatas)
	{
		FRegisteredBulk& TargetRegisteredBulk = Owner->Registry.FindOrAdd(BulkData.GetIdentifier());
		UE::Virtualization::FVirtualizedUntypedBulkData& TargetBulkData = TargetRegisteredBulk.BulkData;
		if (!TargetBulkData.GetIdentifier().IsValid())
		{
			TargetBulkData = BulkData;
			TargetRegisteredBulk.PackageNameToUpdate = BulkData.HasPlaceholderPayloadId() ? PackageName : NAME_None;
		}
		else
		{
			check(TargetBulkData.GetIdentifier() == BulkData.GetIdentifier());
			if (!BulkData.HasPlaceholderPayloadId() && TargetBulkData.HasPlaceholderPayloadId())
			{
				// BULKDATAREGISTRY_TODO: Verify the payloads are the same
				// && TargetBulkData.PayloadInfoMatches(BulkData)
				TargetBulkData = BulkData;
				// We now know that we don't need to update the package with the RawHash, so remove the PackageNameToUpdate
				TargetRegisteredBulk.PackageNameToUpdate = NAME_None;
			}
		}
	}
}

bool FPendingPackage::IsReadFinished() const
{
	return CacheRequest.Poll();
}

void FPendingPackage::UpdateCache()
{
	CacheRequest.Wait();

	// Remove any duplicates in the runtime BulkDatas; elements later in the list were added after and override earlier elements
	{
		TSet<FGuid> BulkDataGuids;
		for (int32 Index = BulkDatas.Num()-1; Index >= 0; --Index)
		{
			bool bAlreadyExists;
			BulkDataGuids.Add(BulkDatas[Index].GetIdentifier(), &bAlreadyExists);
			if (bAlreadyExists)
			{
				BulkDatas.RemoveAt(Index);
			}
		}
	}

	// If the current list of BulkDatas has any elements not in the cache, update the cache
	TMap<FGuid, UE::Virtualization::FVirtualizedUntypedBulkData*> GuidToCachedBulkData;
	for (UE::Virtualization::FVirtualizedUntypedBulkData& BulkData : CachedBulkDatas)
	{
		GuidToCachedBulkData.Add(BulkData.GetIdentifier(), &BulkData);
	}

	// Update any existing BulkDatas and add on any new ones
	bool bUpdatedExisting = false;
	TArray<UE::Virtualization::FVirtualizedUntypedBulkData*> BulkDatasToAdd;
	for (UE::Virtualization::FVirtualizedUntypedBulkData& BulkData : BulkDatas)
	{
		UE::Virtualization::FVirtualizedUntypedBulkData** CachedBulkData = GuidToCachedBulkData.Find(BulkData.GetIdentifier());
		if (CachedBulkData)
		{
			UE::Virtualization::FVirtualizedUntypedBulkData& Cached = **CachedBulkData;
			if (Cached.HasPlaceholderPayloadId() && !BulkData.HasPlaceholderPayloadId())
				// BULKDATAREGISTRY_TODO: Also update if the payloads are different
				// || !TargetBulkData.PayloadInfoMatches(BulkData)
			{
				bUpdatedExisting = true;
				Cached = BulkData;
			}
		}
		else
		{
			BulkDatasToAdd.Add(&BulkData);
		}
	}

	if (bUpdatedExisting || BulkDatasToAdd.Num() > 0)
	{
		// Add on all the new ones
		GuidToCachedBulkData.Reset(); // This map is now invalidated since we are modifying the array it has pointers into
		for (UE::Virtualization::FVirtualizedUntypedBulkData* BulkData : BulkDatasToAdd)
		{
			CachedBulkDatas.Add(*BulkData);
		}

		// Sort the list by guid, to avoid indeterminism in the list
		CachedBulkDatas.Sort([](const UE::Virtualization::FVirtualizedUntypedBulkData& A, const UE::Virtualization::FVirtualizedUntypedBulkData& B)
			{
				return A.GetIdentifier() < B.GetIdentifier();
			});

		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Serialize(Writer, CachedBulkDatas);
		// BULKDATAREGISTRY_TODO: Store the request in a list of pending writes so that we don't try to write a new entry for the package
		// until the previous entry has finished storing, to avoid stale data overwriting new data
		UE::EditorDomain::PutBulkDataList(PackageName, MakeSharedBufferFromArray(MoveTemp(Bytes)));
	}
}

FUpdatePayloadWorker::FUpdatePayloadWorker(FBulkDataRegistryEditorDomain* InBulkDataRegistry,
	const UE::Virtualization::FVirtualizedUntypedBulkData& InSourceBulk)
	: BulkData(InSourceBulk)
	, BulkDataRegistry(InBulkDataRegistry)
{
	SharedDataLock = InBulkDataRegistry->SharedDataLock;
}

void FUpdatePayloadWorker::DoWork()
{
	FUpdatingPayload LocalUpdatingPayload;
	FCompressedBuffer Buffer;
	bool bValid = true;
	for (;;)
	{
		BulkData.UpdatePayloadId();
		Buffer = BulkData.GetCompressedPayload().Get();

		{
			FReadScopeLock SharedDataScopeLock(SharedDataLock->ActiveLock);
			if (!SharedDataLock->bActive)
			{
				// The BulkDataRegistry has destructed. Our list of requesters is on the BulkDataRegistry, so there's nothing we can do except exit
				return;
			}
			FWriteScopeLock RegistryScopeLock(BulkDataRegistry->RegistryLock);

			if (!BulkDataRegistry->UpdatingPayloads.RemoveAndCopyValue(BulkData.GetIdentifier(), LocalUpdatingPayload))
			{
				// The updating payload might not exist in the case of the Registry shutting down; it will clear the UpdatingPayloads to cancel our action
				// Return canceled (which we treat the same as failed) to our requesters
				bValid = false;
				break;
			}
			check(BulkDataRegistry->bActive); // Only set to false at the same time as SharedDataLock->bActive

			FRegisteredBulk* RegisteredBulk = BulkDataRegistry->Registry.Find(BulkData.GetIdentifier());
			if (!RegisteredBulk)
			{
				// Some agent has deregistered the BulkData before we finished calculating its payload
				// return failure to our requesters
				bValid = false;
				break;
			}

			// BULKDATAREGISTRY_TODO: Implement LocationMatches
			if (false) // !BulkData.LocationMatches(BulkData));
			{
				// Some caller has assigned a new BulkData. We need to abandon the BulkData we just loaded and give our callers the
				// information about the new one
				check(RegisteredBulk->BulkData.GetIdentifier() == BulkData.GetIdentifier()); // The identifier in the BulkData should match the key for that BulkData in Registry
				BulkData = RegisteredBulk->BulkData;
				// Add our LocalUpdatingPayload back to UpdatingPayloads; we removed it because we thought we were done.
				BulkDataRegistry->UpdatingPayloads.Add(BulkData.GetIdentifier(), MoveTemp(LocalUpdatingPayload));
				continue;
			}

			// Store the new payload in the Registry's entry for the BulkData; new MetaData requests will no longer need to wait for it
			RegisteredBulk->BulkData = BulkData;

			// Mark that the next GetData call should remove the temporary payload
			RegisteredBulk->bHasTempPayload = true;
			BulkDataRegistry->AddTempLoadedPayload(BulkData.GetIdentifier(), BulkData.GetPayloadSize());
			BulkDataRegistry->PruneTempLoadedPayloads();

			// If the BulkData is marked to be stored in a package's BulkDataList, add an update task for the package.
			// We have to enter the two locks at the same time, because it is only while inside the RegistryScopeLock that we are guaranteed our work has not been canceled.
			if (!RegisteredBulk->PackageNameToUpdate.IsNone())
			{
				FScopeLock PendingPackageScopeLock(&BulkDataRegistry->PendingPackageLock);
				BulkDataRegistry->AddPendingPackageBulkDataInsideLock(RegisteredBulk->PackageNameToUpdate, BulkData);
				if (!BulkData.HasPlaceholderPayloadId())
				{
					// If the BulkData already has its PayloadId, then we no longer need to update it after adding it this time
					RegisteredBulk->PackageNameToUpdate = NAME_None;
				}
			}
			break;
		}
	}

	if (!bValid)
	{
		Buffer.Reset();
	}
	for (TUniqueFunction<void(bool, const FCompressedBuffer&)>& Requester : LocalUpdatingPayload.Requesters)
	{
		Requester(bValid, Buffer);
	}
}

void Serialize(FArchive& Ar, TArray<UE::Virtualization::FVirtualizedUntypedBulkData>& Datas)
{
	int32 Num = Datas.Num();
	Ar << Num;

	const uint32 MinSize = 4;
	if (Ar.IsLoading())
	{
		if (Ar.IsError() || Num * MinSize > Ar.TotalSize() - Ar.Tell())
		{
			Ar.SetError();
			Datas.Empty();
			return;
		}
		else
		{
			Datas.Empty(Num);
		}
		for (int32 n = 0; n < Num; ++n)
		{
			UE::Virtualization::FVirtualizedUntypedBulkData& BulkData = Datas.Emplace_GetRef();
			BulkData.SerializeForRegistry(Ar);
		}
	}
	else
	{
		for (UE::Virtualization::FVirtualizedUntypedBulkData& BulkData : Datas)
		{
			BulkData.SerializeForRegistry(Ar);
		}
	}
}

} // namespace UE::BulkDataRegistry::Private
