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
	// We piggyback on the BulkDataRegistry hook to set the pointer to tunnel in the pointer to the EditorBuildInputResolver as well
	SetGlobalBuildInputResolver(&UE::DerivedData::FEditorBuildInputResolver::Get());
}

FBulkDataRegistryEditorDomain::~FBulkDataRegistryEditorDomain()
{
	SetGlobalBuildInputResolver(nullptr);

	{
		FScopeLock PendingPackageScopeLock(&PendingPackageLock);
		bAllowCacheUpdates = false;
	}
	PendingPackages.Empty();

	FCoreDelegates::OnPostEngineInit.RemoveAll(this);
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
	Registry.Add(BulkData.GetIdentifier(), FBulkSource(MoveTemp(CopyBulk), PackageNameToUpdate));
}

void FBulkDataRegistryEditorDomain::OnExitMemory(const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData)
{
	const FGuid& Key = BulkData.GetIdentifier();
	FWriteScopeLock RegistryScopeLock(RegistryLock);
	FBulkSource* Existing = Registry.Find(Key);
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
	TPromise< UE::BulkDataRegistry::FMetaData> Promise;
	UE::BulkDataRegistry::FMetaData Result;
	Result.bValid = false;
	auto TryExistingCompleteResult = [&BulkDataId, this, &Result](FBulkSource*& OutExisting)
	{
		OutExisting = Registry.Find(BulkDataId);
		if (!OutExisting)
		{
			Result.bValid = false;
			Result.RawHash = FIoHash();
			Result.RawSize = 0;
			return true;
		}

		if (!OutExisting->BulkData.HasPlaceholderPayloadId())
		{
			Result.bValid = true;
			Result.RawHash = OutExisting->BulkData.GetPayloadId().GetIdentifier();
			Result.RawSize = OutExisting->BulkData.GetPayloadSize();
			return true;
		}

		return false;
	};

	TOptional<UE::Virtualization::FVirtualizedUntypedBulkData> CopyBulk;
	FName PackageNameToUpdate;
	{
		FReadScopeLock RegistryScopeLock(RegistryLock);
		FBulkSource* Existing = nullptr;
		if (TryExistingCompleteResult(Existing))
		{
			Promise.SetValue(MoveTemp(Result));
			return Promise.GetFuture();
		}

		CopyBulk.Emplace(Existing->BulkData);
		PackageNameToUpdate = Existing->PackageNameToUpdate;
	}

	CopyBulk->UpdatePayloadId();
	Result.bValid = true;
	Result.RawHash= CopyBulk->GetPayloadId().GetIdentifier();
	Result.RawSize = CopyBulk->GetPayloadSize();

	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		FBulkSource* Existing;
		if (TryExistingCompleteResult(Existing))
		{
			// Some other thread has beaten us to updating the RawHash
			Promise.SetValue(MoveTemp(Result));
			return Promise.GetFuture();
		}

		Existing->BulkData = *CopyBulk;
		Existing->PackageNameToUpdate = NAME_None;

		AddTempLoadedPayload(CopyBulk->GetIdentifier(), CopyBulk->GetPayloadSize());
		PruneTempLoadedPayloads();
	}

	if (!PackageNameToUpdate.IsNone())
	{
		AddPendingPackageBulkData(PackageNameToUpdate, *CopyBulk);
	}

	Promise.SetValue(MoveTemp(Result));
	return Promise.GetFuture();
}

TFuture<UE::BulkDataRegistry::FData> FBulkDataRegistryEditorDomain::GetData(const FGuid& BulkDataId)
{
	TPromise<UE::BulkDataRegistry::FData> Result;
	TOptional<UE::Virtualization::FVirtualizedUntypedBulkData> CopyBulk;
	FName PackageNameToUpdate;
	bool bNeedsUpdate = false;
	{
		FReadScopeLock RegistryScopeLock(RegistryLock);
		FBulkSource* Existing = Registry.Find(BulkDataId);
		if (!Existing)
		{
			Result.SetValue(UE::BulkDataRegistry::FData{ false, FCompressedBuffer() });
			return Result.GetFuture();
		}
		CopyBulk.Emplace(Existing->BulkData);
		PackageNameToUpdate = Existing->PackageNameToUpdate;
		bNeedsUpdate = CopyBulk->HasPlaceholderPayloadId();
	}

	if (bNeedsUpdate)
	{
		CopyBulk->UpdatePayloadId();
	}
	FCompressedBuffer Payload = CopyBulk->GetCompressedPayload().Get();
	bool bUpdatedByGetPayload = true; // BULKDATAREGISTRY_TODO: Calculate this and return it from GetPayload
	bool bChanged = bNeedsUpdate || bUpdatedByGetPayload;
	CopyBulk->UnloadData();

	if (bChanged)
	{
		FWriteScopeLock RegistryScopeLock(RegistryLock);
		FBulkSource* Existing = Registry.Find(BulkDataId);
		if (Existing)
		{
			bNeedsUpdate = Existing->BulkData.HasPlaceholderPayloadId();
			Existing->BulkData = *CopyBulk;
			Existing->PackageNameToUpdate = NAME_None;
		}
	}

	if (bNeedsUpdate && !PackageNameToUpdate.IsNone())
	{
		AddPendingPackageBulkData(PackageNameToUpdate, *CopyBulk);
	}

	Result.SetValue(UE::BulkDataRegistry::FData{ true, MoveTemp(Payload) });
	return Result.GetFuture();
}

void FBulkDataRegistryEditorDomain::TickCook(float DeltaTime, bool bTickComplete)
{
	bool bWaitForCooldown = !bTickComplete;
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
			FBulkSource* Existing = Registry.Find(Payload.Guid);
			if (Existing)
			{
				// UnloadData only unloads the in-memory data, and only if the BulkData can be reloaded from disk
				Existing->BulkData.UnloadData();
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
	if (!Owner->bAllowCacheUpdates)
	{
		return;
	}
	for (const UE::Virtualization::FVirtualizedUntypedBulkData& BulkData : CachedBulkDatas)
	{
		FBulkSource& TargetBulkSource = Owner->Registry.FindOrAdd(BulkData.GetIdentifier());
		UE::Virtualization::FVirtualizedUntypedBulkData& TargetBulkData = TargetBulkSource.BulkData;
		if (!TargetBulkData.GetIdentifier().IsValid())
		{
			TargetBulkData = BulkData;
			TargetBulkSource.PackageNameToUpdate = BulkData.HasPlaceholderPayloadId() ? PackageName : NAME_None;
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
				TargetBulkSource.PackageNameToUpdate = NAME_None;
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

	// Remove any duplicates in the runtime BulkDatas; elements later in the list override were added after and override earlier elements
	{
		TSet<FGuid> BulkDataGuids;
		for (int32 Index = BulkDataGuids.Num(); Index >= 0; --Index)
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
		Serialize(Writer, BulkDatas);
		// BULKDATAREGISTRY_TODO: Store the request in a list of pending writes so that we don't try to write a new entry for the package
		// until the previous entry has finished storing, to avoid stale data overwriting new data
		UE::EditorDomain::PutBulkDataList(PackageName, MakeSharedBufferFromArray(MoveTemp(Bytes)));
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
