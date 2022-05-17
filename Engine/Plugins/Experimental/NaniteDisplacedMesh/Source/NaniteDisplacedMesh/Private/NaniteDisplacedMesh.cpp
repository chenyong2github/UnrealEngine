// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteDisplacedMesh.h"
#include "Modules/ModuleManager.h"
#include "Engine/StaticMesh.h"

#if WITH_EDITOR

#include "DerivedDataCache.h"
#include "DerivedDataRequestOwner.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Serialization/MemoryHasher.h"

class FNaniteBuildAsyncCacheTask
{
public:
	FNaniteBuildAsyncCacheTask(
		const FIoHash& InKeyHash,
		Nanite::FResources* InResource,
		UNaniteDisplacedMesh& InDisplacedMesh,
		const ITargetPlatform* TargetPlatform
	);

	inline void Wait() { Owner.Wait(); }
	inline bool Poll() const { return Owner.Poll(); }

private:
	void BeginCache(const FIoHash& KeyHash);
	void EndCache(UE::DerivedData::FCacheGetValueResponse&& Response);
	void BuildResource(const UE::DerivedData::FSharedString& Name, const UE::DerivedData::FCacheKey& Key);

private:
	Nanite::FResources* Resource;
	UNaniteDisplacedMesh& DisplacedMesh;
	UE::DerivedData::FRequestOwner Owner;
};

FNaniteBuildAsyncCacheTask::FNaniteBuildAsyncCacheTask(
	const FIoHash& InKeyHash,
	Nanite::FResources* InResource,
	UNaniteDisplacedMesh& InDisplacedMesh,
	const ITargetPlatform* TargetPlatform
)
	: Resource(InResource)
	, DisplacedMesh(InDisplacedMesh)
	, Owner(UE::DerivedData::EPriority::Normal)
{
	BeginCache(InKeyHash);
}

void FNaniteBuildAsyncCacheTask::BeginCache(const FIoHash& KeyHash)
{
	using namespace UE::DerivedData;
	static const FCacheBucket Bucket("NaniteDisplacedMesh");
	GetCache().GetValue({{{DisplacedMesh.GetPathName()}, {Bucket, KeyHash}}}, Owner,
		[this](FCacheGetValueResponse&& Response) { EndCache(MoveTemp(Response)); });
}

void FNaniteBuildAsyncCacheTask::EndCache(UE::DerivedData::FCacheGetValueResponse&& Response)
{
	using namespace UE::DerivedData;

	if (Response.Status == EStatus::Ok)
	{
		((IRequestOwner&)Owner).LaunchTask(TEXT("NaniteDisplacedMeshSerialize"), [this, Value = MoveTemp(Response.Value)]
		{
			FSharedBuffer Data = Value.GetData().Decompress();
			FMemoryReaderView Ar(Data, /*bIsPersistent*/ true);
			Resource->Serialize(Ar, &DisplacedMesh, /*bCooked*/ false);
		});
	}
	else if (Response.Status == EStatus::Error)
	{
		((IRequestOwner&)Owner).LaunchTask(TEXT("NaniteDisplacedMeshBuild"), [this, Name = Response.Name, Key = Response.Key]
		{
			BuildResource(Name, Key);
		});
	}
}

void FNaniteBuildAsyncCacheTask::BuildResource(const UE::DerivedData::FSharedString& Name, const UE::DerivedData::FCacheKey& Key)
{
	// TODO: Build into this->Resource...
	
	{
		TArray64<uint8> Data;
		FMemoryWriter64 Ar(Data, /*bIsPersistent*/ true);
		Resource->Serialize(Ar, &DisplacedMesh, /*bCooked*/ false);

		using namespace UE::DerivedData;
		GetCache().PutValue({ {Name, Key, FValue::Compress(MakeSharedBufferFromArray(MoveTemp(Data)))} }, Owner);
	}
}

#endif

UNaniteDisplacedMesh::UNaniteDisplacedMesh(const FObjectInitializer& Init)
: Super(Init)
{
}

void UNaniteDisplacedMesh::PostLoad()
{
#if WITH_EDITOR
	if (FApp::CanEverRender() && ResourcesKeyHash.IsZero())
	{
		if (ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform(); ensure(RunningPlatform))
		{
			BeginCacheDerivedData(RunningPlatform);
		}
	}
#endif

	Super::PostLoad();
}

void UNaniteDisplacedMesh::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsFilterEditorOnly() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
	#if WITH_EDITOR
		if (Ar.IsCooking())
		{
			CacheDerivedData(Ar.CookingTarget()).Serialize(Ar, this, /*bCooked*/ true);
		}
		else
	#endif
		{
			Resources.Serialize(Ar, this, /*bCooked*/ true);
		}
	}
}

void UNaniteDisplacedMesh::InitResources()
{
	// TODO: Initialize NaniteResources and register with Nanite streaming manager
}

void UNaniteDisplacedMesh::ReleaseResources()
{
	// TODO: Release NaniteResources
}

#if WITH_EDITOR

void UNaniteDisplacedMesh::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	if (ResourcesKeyHash != CreateDerivedDataKeyHash(RunningPlatform))
	{
		BeginCacheDerivedData(RunningPlatform);
	}
}

void UNaniteDisplacedMesh::BeginCacheForCookedPlatformData(const ITargetPlatform* TargetPlatform)
{
	Super::BeginCacheForCookedPlatformData(TargetPlatform);
	BeginCacheDerivedData(TargetPlatform);
}

bool UNaniteDisplacedMesh::IsCachedCookedPlatformDataLoaded(const ITargetPlatform* TargetPlatform)
{
	const FIoHash KeyHash = CreateDerivedDataKeyHash(TargetPlatform);
	if (PollCacheDerivedData(KeyHash))
	{
		EndCacheDerivedData(KeyHash);
		return true;
	}

	return false;
}

void UNaniteDisplacedMesh::ClearAllCachedCookedPlatformData()
{
	// Delete any cache tasks first because the destructor will cancel the cache and build tasks,
	// and drop their pointers to the resources.
	CacheTasksByKeyHash.Empty();
	ResourcesByPlatformKeyHash.Empty();
	Super::ClearAllCachedCookedPlatformData();
}

FIoHash UNaniteDisplacedMesh::CreateDerivedDataKeyHash(const ITargetPlatform* TargetPlatform)
{
	FMemoryHasherBlake3 Writer;

	FGuid DisplacedMeshVersionGuid(0xBDBC804E, 0x8D374ECD, 0xBF596C43, 0xACBEFC27);
	Writer << DisplacedMeshVersionGuid;

	FGuid NaniteVersionGuid = FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().NANITE_DERIVEDDATA_VER);
	Writer << NaniteVersionGuid;

	const FStaticMeshLODSettings& PlatformLODSettings = TargetPlatform->GetStaticMeshLODSettings();

	if (IsValid(Parameters.BaseMesh))
	{
		const FStaticMeshLODGroup& LODGroup = PlatformLODSettings.GetLODGroup(Parameters.BaseMesh->LODGroup);
		FString StaticMeshKey = UE::Private::StaticMesh::BuildStaticMeshDerivedDataKey(TargetPlatform, Parameters.BaseMesh, LODGroup);
		Writer << StaticMeshKey;
	}

	if (IsValid(Parameters.Displacement1))
	{
		FGuid Displacement1Id = Parameters.Displacement1->Source.GetId();
		Writer << Displacement1Id;
	}

	if (IsValid(Parameters.Displacement2))
	{
		FGuid Displacement2Id = Parameters.Displacement2->Source.GetId();
		Writer << Displacement2Id;
	}

	if (IsValid(Parameters.Displacement3))
	{
		FGuid Displacement3Id = Parameters.Displacement3->Source.GetId();
		Writer << Displacement3Id;
	}

	if (IsValid(Parameters.Displacement4))
	{
		FGuid Displacement4Id = Parameters.Displacement4->Source.GetId();
		Writer << Displacement4Id;
	}

	Writer << Parameters.Magnitude1;
	Writer << Parameters.Magnitude2;
	Writer << Parameters.Magnitude3;
	Writer << Parameters.Magnitude4;

	Writer << Parameters.Center1;
	Writer << Parameters.Center2;
	Writer << Parameters.Center3;
	Writer << Parameters.Center4;

	return Writer.Finalize();
}

FIoHash UNaniteDisplacedMesh::BeginCacheDerivedData(const ITargetPlatform* TargetPlatform)
{
	const FIoHash KeyHash = CreateDerivedDataKeyHash(TargetPlatform);

	if (ResourcesKeyHash == KeyHash || ResourcesByPlatformKeyHash.Contains(KeyHash))
	{
		return KeyHash;
	}

	Nanite::FResources* TargetResources = nullptr;
	if (TargetPlatform->IsRunningPlatform())
	{
		ResourcesKeyHash = KeyHash;
		TargetResources = &Resources;
	}
	else
	{
		TargetResources = ResourcesByPlatformKeyHash.Emplace(KeyHash, MakeUnique<Nanite::FResources>()).Get();
	}

	CacheTasksByKeyHash.Emplace(KeyHash, MakePimpl<FNaniteBuildAsyncCacheTask>(KeyHash, TargetResources, *this, TargetPlatform));
	return KeyHash;
}

bool UNaniteDisplacedMesh::PollCacheDerivedData(const FIoHash& KeyHash) const
{
	if (const TPimplPtr<FNaniteBuildAsyncCacheTask>* Task = CacheTasksByKeyHash.Find(KeyHash))
	{
		return (*Task)->Poll();
	}

	return true;
}

void UNaniteDisplacedMesh::EndCacheDerivedData(const FIoHash& KeyHash)
{
	TPimplPtr<FNaniteBuildAsyncCacheTask> Task;
	if (CacheTasksByKeyHash.RemoveAndCopyValue(KeyHash, Task))
	{
		Task->Wait();
	}
}

Nanite::FResources& UNaniteDisplacedMesh::CacheDerivedData(const ITargetPlatform* TargetPlatform)
{
	const FIoHash KeyHash = BeginCacheDerivedData(TargetPlatform);
	EndCacheDerivedData(KeyHash);
	return ResourcesKeyHash == KeyHash ? Resources : *ResourcesByPlatformKeyHash[KeyHash];
}

#endif // WITH_EDITOR
