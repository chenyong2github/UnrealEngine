// Copyright Epic Games, Inc. All Rights Reserved.

#include "BulkDataRegistryEditorDomain.h"

#include "Serialization/BulkDataRegistry.h"
#include "Compression/CompressedBuffer.h"
#include "DerivedDataCache.h"
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
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace UE::BulkDataRegistry::Private
{

namespace Constants
{
	constexpr uint64 TempLoadedPayloadsSizeBudget = 1024 * 1024 * 100;
	constexpr double TempLoadedPayloadsDuration = 60.;
}

TConstArrayView<uint8> MakeArrayView(FSharedBuffer Buffer)
{
	return TConstArrayView<uint8>(reinterpret_cast<const uint8*>(Buffer.GetData()), Buffer.GetSize());
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

	// Since the UpdatingPayloads AsyncTasks can no longer access their Requesters, we have to call those callbacks
	for (TPair<FGuid, FUpdatingPayload>& Pair : LocalUpdatingPayloads)
	{
		for (TUniqueFunction<void(bool, const FCompressedBuffer&)>& Requester : Pair.Value.Requesters)
		{
			Requester(false, FCompressedBuffer());
		}
	}
	LocalUpdatingPayloads.Empty();

	// Clear PendingPackages
	TMap<FName, TUniquePtr<FPendingPackage>> LocalPendingPackages;
	{
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);
		// Take custody of PendingPackages
		Swap(LocalPendingPackages, PendingPackages);
	}
	for (TPair<FName, TUniquePtr<FPendingPackage>>& PendingPackagePair : LocalPendingPackages)
	{
		PendingPackagePair.Value->Cancel();
	}
	LocalPendingPackages.Empty();

	// Clear PendingPackagePayloadIds. We have to take custody of PendingPayloadIds after calling
	// Cancel from all FPendingPackage, as the FPendingPackages have callbacks that may write to PendingPayloadIds
	TMap<FGuid, TRefCountPtr<FPendingPayloadId>> LocalPendingPayloadIds;
	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		// Take custody of PendingPayloadIds
		Swap(LocalPendingPayloadIds, PendingPayloadIds);
	}
	for (TPair<FGuid, TRefCountPtr<FPendingPayloadId>>& PendingPayloadIdPair : LocalPendingPayloadIds)
	{
		PendingPayloadIdPair.Value->Cancel();
	}
}

void FBulkDataRegistryEditorDomain::OnEndLoadPackage(TConstArrayView<UPackage*> LoadedPackages)
{
	TArray<TUniquePtr<FPendingPackage>> PackagesToWrite;
	{
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);

		for (UPackage* LoadedPackage : LoadedPackages)
		{
			FName PackageName = LoadedPackage->GetFName();
			uint32 KeyHash = GetTypeHash(PackageName);
			TUniquePtr<FPendingPackage>* PendingPackage = PendingPackages.FindByHash(KeyHash, PackageName);
			if (PendingPackage)
			{
				bool bShouldRemove;
				bool bShouldWriteCache;
				check(PendingPackage->IsValid());
				(*PendingPackage)->OnEndLoad(bShouldRemove, bShouldWriteCache);
				// We do not hold a lock when calling WriteCache, so we require that the package no longer
				// be accessible to other threads through PendingPackages if we need to write the cache,
				// so bShouldRemove must be true if bShouldWriteCache is
				check(!bShouldWriteCache || bShouldRemove);
				if (bShouldRemove)
				{
					if (bShouldWriteCache)
					{
						PackagesToWrite.Add(MoveTemp(*PendingPackage));
					}
					PendingPackages.RemoveByHash(KeyHash, PackageName);
				}
			}
		}
	}

	for (TUniquePtr<FPendingPackage>& Package : PackagesToWrite)
	{
		Package->WriteCache();
	}
}

void FBulkDataRegistryEditorDomain::Register(UPackage* Owner, const UE::Serialization::FEditorBulkData& BulkData)
{
	if (!BulkData.GetIdentifier().IsValid())
	{
		return;
	}

	FName PackageName = NAME_None;
	UE::Serialization::FEditorBulkData CopyBulk(BulkData.CopyTornOff());
	if (Owner
		&& Owner->GetFileSize() // We only record the BulkDataList for disk packages
		&& !Owner->GetHasBeenEndLoaded() // We only record BulkDatas that are loaded before the package finishes loading
		&& CopyBulk.CanSaveForRegistry()
		)
	{
		PackageName = Owner->GetFName();
		AddPendingPackageBulkData(PackageName, CopyBulk);
	}

	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		check(bActive); // Registrations should not come in after we destruct
		Registry.Add(BulkData.GetIdentifier(), FRegisteredBulk(MoveTemp(CopyBulk), PackageName));
	}
	ResaveSizeTracker.Register(Owner, BulkData);
}

void FBulkDataRegistryEditorDomain::OnExitMemory(const UE::Serialization::FEditorBulkData& BulkData)
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

		UE::Serialization::FEditorBulkData& BulkData = Existing->BulkData;
		if (!BulkData.HasPlaceholderPayloadId())
		{
			TPromise<UE::BulkDataRegistry::FMetaData> Promise;
			Promise.SetValue(UE::BulkDataRegistry::FMetaData{ true, BulkData.GetPayloadId(), static_cast<uint64>(BulkData.GetPayloadSize()) });
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
	TOptional<UE::Serialization::FEditorBulkData> CopyBulk;
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
				// The contract of FEditorBulkData does not guarantee that GetCompressedPayload() is a quick operation (it may load the data
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
	// read from the BulkData. However, the contract of FEditorBulkData guarantees that the TFuture gets a copy of all data it needs and
	// does not read from the BulkData after returning from GetCompressedPayload, so a read-after-free is not possible.
	return CopyBulk->GetCompressedPayload().Next([](FCompressedBuffer Payload)
		{
			return UE::BulkDataRegistry::FData{ true, Payload };
		});
}

uint64 FBulkDataRegistryEditorDomain::GetBulkDataResaveSize(FName PackageName)
{
	return ResaveSizeTracker.GetBulkDataResaveSize(PackageName);
}

void FBulkDataRegistryEditorDomain::TickCook(float DeltaTime, bool bTickComplete)
{
	bool bWaitForCooldown = !bTickComplete;
	check(bActive); // Ticks should not come in after we destruct

	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		PruneTempLoadedPayloads();
	}
}

void FBulkDataRegistryEditorDomain::Tick(float DeltaTime)
{
	TickCook(DeltaTime, false /* bTickComplete */);
}

void FBulkDataRegistryEditorDomain::AddPendingPackageBulkData(FName PackageName, const UE::Serialization::FEditorBulkData& BulkData)
{
	FScopeLock PendingPackageScopeLock(&PendingPackageLock);
	check(bActive); // Registrations should not come in after we destruct, and AsyncTasks should check bActive before calling
	TUniquePtr<FPendingPackage>& PendingPackage = PendingPackages.FindOrAdd(PackageName);
	if (!PendingPackage.IsValid())
	{
		PendingPackage = MakeUnique<FPendingPackage>(PackageName, this);
	}
	if (!PendingPackage->IsLoadInProgress())
	{
		return;
	}
	PendingPackage->AddBulkData(BulkData);
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

void FBulkDataRegistryEditorDomain::WritePayloadIdToCache(FName PackageName, const UE::Serialization::FEditorBulkData& BulkData) const
{
	check(!PackageName.IsNone());
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	const_cast<UE::Serialization::FEditorBulkData&>(BulkData).SerializeForRegistry(Writer);
	UE::EditorDomain::PutBulkDataPayloadId(PackageName, BulkData.GetIdentifier(), MakeSharedBufferFromArray(MoveTemp(Bytes)));
}

void FBulkDataRegistryEditorDomain::ReadPayloadIdsFromCache(FName PackageName, TArray<TRefCountPtr<FPendingPayloadId>>&& OldPendings,
	TArray<TRefCountPtr<FPendingPayloadId>>&& NewPendings)
{
	// Cancel any old requests for the Guids in NewPendings; we are about to overwrite them
	// This cancellation has to occur outside of any lock, since the task may be in progress and
	// and to enter the lock and Cancel will wait on it
	for (TRefCountPtr<FPendingPayloadId>& OldPending : OldPendings)
	{
		OldPending->Cancel();
	}
	OldPendings.Empty();
	for (TRefCountPtr<FPendingPayloadId>& NewPending : NewPendings)
	{
		// Creation of the Request has to occur outside of any lock, because the request
		// may execute immediately on this thread and need to enter the lock; our locks are non-reentrant
		UE::DerivedData::FRequestBarrier Barrier(NewPending->GetRequestOwner());
		UE::EditorDomain::GetBulkDataPayloadId(PackageName, NewPending->GetBulkDataId(), NewPending->GetRequestOwner(),
			[this, PackageName, NewPending](FSharedBuffer Buffer)
		{
			if (Buffer.IsNull())
			{
				return;
			}
			FMemoryReaderView Reader(MakeArrayView(Buffer));
			UE::Serialization::FEditorBulkData CachedBulkData;
			CachedBulkData.SerializeForRegistry(Reader);
			const FGuid& BulkDataId = NewPending->GetBulkDataId();
			if (Reader.IsError() || CachedBulkData.GetIdentifier() != BulkDataId)
			{
				UE_LOG(LogEditorDomain, Warning, TEXT("Corrupt cache data for BulkDataPayloadId %s."), WriteToString<192>(PackageName, TEXT("/"), BulkDataId).ToString());
				return;
			}

			FWriteScopeLock RegistryScopeLock(RegistryLock);
			if (!bActive)
			{
				return;
			}
			TRefCountPtr<FPendingPayloadId> ExistingPending;
			if (!PendingPayloadIds.RemoveAndCopyValue(BulkDataId, ExistingPending))
			{
				return;
			}
			check(ExistingPending->GetBulkDataId() == BulkDataId);
			if (ExistingPending != NewPending)
			{
				// We removed ExistingPending because we thought it was equal to NewPending, but it's not, so put it back
				PendingPayloadIds.Add(BulkDataId, MoveTemp(ExistingPending));
				return;
			}

			FRegisteredBulk* ExistingRegisteredBulk = Registry.Find(BulkDataId);
			if (!ExistingRegisteredBulk)
			{
				return;
			}

			UE::Serialization::FEditorBulkData& ExistingBulkData = ExistingRegisteredBulk->BulkData;
			check(ExistingBulkData.GetIdentifier() == BulkDataId);
			if (ExistingBulkData.HasPlaceholderPayloadId())
			{
				// BULKDATAREGISTRY_TODO: Implement LocationMatches
				//&& CachedBulkData.LocationMatches(ExistingBulkData);

				ExistingBulkData = CachedBulkData;
			}
		});
	}

	// Assign the Requests we just created to the data for each guid in the map,
	// which has to be edited only within the lock
	// If for any reason (race condition, shutting down) we can not assign a Request,
	// we have to cancel the request before returning, to make sure the callback does not hold a pointer
	// to this that could become dangling.
	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		if (!bActive)
		{
			for (TRefCountPtr<FPendingPayloadId>& NewPending : NewPendings)
			{
				OldPendings.Add(MoveTemp(NewPending));
			}
		}
		else
		{
			for (TRefCountPtr<FPendingPayloadId>& NewPending : NewPendings)
			{
				TRefCountPtr<FPendingPayloadId>* ExistingPending = PendingPayloadIds.Find(NewPending->GetBulkDataId());
				if (!ExistingPending || *ExistingPending != NewPending)
				{
					OldPendings.Add(MoveTemp(NewPending));
				}
			}
		}
	}
	NewPendings.Empty();
	for (TRefCountPtr<FPendingPayloadId>& OldPending : OldPendings)
	{
		OldPending->Cancel();
	}
	OldPendings.Empty();
}

FPendingPackage::FPendingPackage(FName InPackageName, FBulkDataRegistryEditorDomain* InOwner)
	: PackageName(InPackageName)
	, BulkDataListCacheRequest(UE::DerivedData::EPriority::Low)
	, Owner(InOwner)
{
	PendingOperations = Flag_EndLoad | Flag_BulkDataListResults;

	UE::EditorDomain::GetBulkDataList(PackageName, BulkDataListCacheRequest,
		[this](FSharedBuffer Buffer) { OnBulkDataListResults(Buffer); });
}

void FPendingPackage::Cancel()
{
	// Called from outside Owner->PendingPackagesLock, so OnBulkDataList can complete on other thread while we wait
	// Called after removing this from Owner->PendingPackages under a previous cover of the lock
	// If OnBulkDataList is running on other thread its attempt to remove from PendingPackages will be a noop
	if (!BulkDataListCacheRequest.Poll())
	{
		// Optimization: prevent WriteCache from running at all if we reach here first
		PendingOperations.fetch_or(Flag_Canceled);
		BulkDataListCacheRequest.Cancel();
	}
}

void FPendingPackage::OnEndLoad(bool& bOutShouldRemove, bool& bOutShouldWriteCache)
{
	// Called from within Owner->PendingPackageLock
	bLoadInProgress = false;
	if (PendingOperations.fetch_and(~Flag_EndLoad) == Flag_EndLoad)
	{
		bOutShouldWriteCache = true;
		bOutShouldRemove = true;
	}
	else
	{
		bOutShouldWriteCache = false;
		bOutShouldRemove = false;
	}
}

void FPendingPackage::OnBulkDataListResults(FSharedBuffer Buffer)
{
	if (!Buffer.IsNull())
	{
		FMemoryReaderView Reader(MakeArrayView(Buffer));
		Serialize(Reader, CachedBulkDatas);
		if (Reader.IsError())
		{
			CachedBulkDatas.Empty();
		}
	}

	ReadCache();

	if (PendingOperations.fetch_and(~Flag_BulkDataListResults) == Flag_BulkDataListResults)
	{
		// We do not hold a lock when writing the cache, so we need to remove this from PendingPackages 
		// before calling WriteCache to avoid other threads being able to access it.
		TUniquePtr<FPendingPackage> ThisPointer;
		{
			FScopeLock PendingPackageScopeLock(&Owner->PendingPackageLock);
			ThisPointer = Owner->PendingPackages.FindAndRemoveChecked(PackageName);
		}
		WriteCache();

		// Deleting *this will destruct BulkDataListCacheRequest, which by
		// default calls Cancel, but Cancel will block on this callback we are
		// currently in. Direct the owner to keep requests alive to avoid that deadlock.
		BulkDataListCacheRequest.KeepAlive();
		ThisPointer.Reset();
		// *this has been deleted and can no longer be accessed
	}
}

void FPendingPackage::ReadCache()
{
	if (CachedBulkDatas.Num() == 0)
	{
		return;
	}

	TArray<TRefCountPtr<FPendingPayloadId>> OldPendings;
	TArray<TRefCountPtr<FPendingPayloadId>> NewPendings;

	// Add each CachedBulkData to the Registry, updating RawHash if it is missing.
	// For every BulkData in this package in the Registry after the CachedBulkData has been added, 
	// if the RawHash is missing from the CachedBulkData as well, queue a read of its RawHash
	// from the separate PlaceholderPayloadId BulkTablePayloadId cache bucket.
	{
		FWriteScopeLock RegistryScopeLock(Owner->RegistryLock);
		if (!Owner->bActive)
		{
			return;
		}
		for (const UE::Serialization::FEditorBulkData& BulkData : CachedBulkDatas)
		{
			FGuid BulkDataId = BulkData.GetIdentifier();
			FRegisteredBulk& TargetRegisteredBulk = Owner->Registry.FindOrAdd(BulkData.GetIdentifier());

			bool bCachedLocationMatches = true;
			UE::Serialization::FEditorBulkData& TargetBulkData = TargetRegisteredBulk.BulkData;
			if (!TargetBulkData.GetIdentifier().IsValid())
			{
				TargetBulkData = BulkData;
				TargetRegisteredBulk.PackageName = PackageName;
			}
			else
			{
				check(TargetBulkData.GetIdentifier() == BulkDataId);
				// BULKDATAREGISTRY_TODO: Implement LocationMatches
				// bCachedLocationMatches = BulkData.LocationMatches(TargetBulkData);
				if (bCachedLocationMatches && !BulkData.HasPlaceholderPayloadId() && TargetBulkData.HasPlaceholderPayloadId())
				{
					TargetBulkData = BulkData;
					TargetRegisteredBulk.PackageName = PackageName;
				}
			}

			if (bCachedLocationMatches && TargetBulkData.HasPlaceholderPayloadId())
			{
				NewPendings.Emplace(new FPendingPayloadId(BulkDataId));
			}
		}

		for (TRefCountPtr<FPendingPayloadId>& NewPending : NewPendings)
		{
			TRefCountPtr<FPendingPayloadId>& ExistingPending = Owner->PendingPayloadIds.FindOrAdd(NewPending->GetBulkDataId());
			if (ExistingPending.IsValid())
			{
				OldPendings.Add(MoveTemp(ExistingPending));
			}
			ExistingPending = NewPending;
		}
	}

	Owner->ReadPayloadIdsFromCache(PackageName, MoveTemp(OldPendings), MoveTemp(NewPendings));
}

void FPendingPackage::WriteCache()
{
	// If the BulkDataList cache read found some existing results, then exit; cache results are deterministic so there
	// is no need to write the list to the cache again.
	if (CachedBulkDatas.Num() > 0)
	{
		return;
	}

	check(BulkDatas.Num() > 0); // We should have >= 1 bulkdatas, or we would not have created the FPendingPackage
	// Remove any duplicates in the runtime BulkDatas; elements later in the list override earlier elements
	{
		TSet<FGuid> BulkDataGuids;
		for (int32 Index = BulkDatas.Num() - 1; Index >= 0; --Index)
		{
			bool bAlreadyExists;
			BulkDataGuids.Add(BulkDatas[Index].GetIdentifier(), &bAlreadyExists);
			if (bAlreadyExists)
			{
				BulkDatas.RemoveAt(Index);
			}
		}
	}

	// Sort the list by guid, to avoid indeterminism in the list
	BulkDatas.Sort([](const UE::Serialization::FEditorBulkData& A, const UE::Serialization::FEditorBulkData& B)
		{
			return A.GetIdentifier() < B.GetIdentifier();
		});

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Serialize(Writer, BulkDatas);
	UE::EditorDomain::PutBulkDataList(PackageName, MakeSharedBufferFromArray(MoveTemp(Bytes)));
}

FUpdatePayloadWorker::FUpdatePayloadWorker(FBulkDataRegistryEditorDomain* InBulkDataRegistry,
	const UE::Serialization::FEditorBulkData& InSourceBulk)
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

			if (!RegisteredBulk->PackageName.IsNone())
			{
				BulkDataRegistry->WritePayloadIdToCache(RegisteredBulk->PackageName, BulkData);
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

FPendingPayloadId::FPendingPayloadId(const FGuid& InBulkDataId)
	: BulkDataId(InBulkDataId)
	, Request(UE::DerivedData::EPriority::Low)
{
	// The last reference to this can be released by the completion callback, which will deadlock
	// trying to cancel the request. KeepAlive skips cancellation in the destructor.
	Request.KeepAlive();
}

void FPendingPayloadId::Cancel()
{
	Request.Cancel();
}

void Serialize(FArchive& Ar, TArray<UE::Serialization::FEditorBulkData>& Datas)
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
			UE::Serialization::FEditorBulkData& BulkData = Datas.Emplace_GetRef();
			BulkData.SerializeForRegistry(Ar);
		}
	}
	else
	{
		for (UE::Serialization::FEditorBulkData& BulkData : Datas)
		{
			BulkData.SerializeForRegistry(Ar);
		}
	}
}

} // namespace UE::BulkDataRegistry::Private
