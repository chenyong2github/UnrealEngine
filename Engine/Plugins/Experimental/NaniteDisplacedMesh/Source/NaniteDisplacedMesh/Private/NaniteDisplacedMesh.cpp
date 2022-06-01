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
#include "Async/Async.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshBuilder.h"
#include "MeshDescriptionHelper.h"
#include "NaniteBuilder.h"
#include "NaniteDisplacedMeshAlgo.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNaniteDisplacedMesh, Log, All);

#if WITH_EDITOR

class FNaniteBuildAsyncCacheTask
{
public:
	FNaniteBuildAsyncCacheTask(
		const FIoHash& InKeyHash,
		FNaniteData* InData,
		UNaniteDisplacedMesh& InDisplacedMesh,
		const ITargetPlatform* TargetPlatform
	);

	inline void Wait() { Owner.Wait(); }
	inline bool Poll() const { return Owner.Poll(); }

private:
	void BeginCache(const FIoHash& KeyHash, const UNaniteDisplacedMesh& DisplacedMesh);
	void EndCache(UE::DerivedData::FCacheGetValueResponse&& Response);
	bool BuildData(const UE::DerivedData::FSharedString& Name, const UE::DerivedData::FCacheKey& Key);
	void InitResources();

private:
	FNaniteData* Data;
	TWeakObjectPtr<UNaniteDisplacedMesh> WeakDisplacedMesh;
	UE::DerivedData::FRequestOwner Owner;
};

FNaniteBuildAsyncCacheTask::FNaniteBuildAsyncCacheTask(
	const FIoHash& InKeyHash,
	FNaniteData* InData,
	UNaniteDisplacedMesh& InDisplacedMesh,
	const ITargetPlatform* TargetPlatform
)
	: Data(InData)
	, WeakDisplacedMesh(&InDisplacedMesh)
	, Owner(UE::DerivedData::EPriority::Normal)
{
	BeginCache(InKeyHash, InDisplacedMesh);
}

void FNaniteBuildAsyncCacheTask::BeginCache(const FIoHash& KeyHash, const UNaniteDisplacedMesh& DisplacedMesh)
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
			if (UNaniteDisplacedMesh* DisplacedMesh = WeakDisplacedMesh.Get())
			{
				FSharedBuffer RecordData = Value.GetData().Decompress();
				FMemoryReaderView Ar(RecordData, /*bIsPersistent*/ true);
				Data->Resources.Serialize(Ar, DisplacedMesh, /*bCooked*/ false);
				Ar << Data->MeshSections;

				InitResources();
			}
		});
	}
	else if (Response.Status == EStatus::Error)
	{
		((IRequestOwner&)Owner).LaunchTask(TEXT("NaniteDisplacedMeshBuild"), [this, Name = Response.Name, Key = Response.Key]
		{
			if (!BuildData(Name, Key))
			{
				return;
			}
			if (UNaniteDisplacedMesh* DisplacedMesh = WeakDisplacedMesh.Get())
			{
				TArray64<uint8> RecordData;
				FMemoryWriter64 Ar(RecordData, /*bIsPersistent*/ true);
				Data->Resources.Serialize(Ar, DisplacedMesh, /*bCooked*/ false);
				Ar << Data->MeshSections;

				GetCache().PutValue({ {Name, Key, FValue::Compress(MakeSharedBufferFromArray(MoveTemp(RecordData)))} }, Owner);

				InitResources();
			}
		});
	}
}

static FStaticMeshSourceModel& GetBaseMeshSourceModel(UStaticMesh& BaseMesh)
{
	const bool bHasHiResSourceModel = BaseMesh.IsHiResMeshDescriptionValid();
	return bHasHiResSourceModel ? BaseMesh.GetHiResSourceModel() : BaseMesh.GetSourceModel(0);
}

bool FNaniteBuildAsyncCacheTask::BuildData(const UE::DerivedData::FSharedString& Name, const UE::DerivedData::FCacheKey& Key)
{
	using namespace UE::DerivedData;

	UNaniteDisplacedMesh* DisplacedMesh = WeakDisplacedMesh.Get();
	if (!DisplacedMesh)
	{
		return false;
	}

	Nanite::IBuilderModule& NaniteBuilderModule = Nanite::IBuilderModule::Get();

	Data->Resources = {};
	Data->MeshSections.Empty();

	UStaticMesh* BaseMesh = DisplacedMesh->Parameters.BaseMesh;

	if (!IsValid(BaseMesh))
	{
		UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Cannot find a valid base mesh to build the displaced mesh asset."));
		return false;
	}

	if (!BaseMesh->IsMeshDescriptionValid(0))
	{
		UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Cannot find a valid mesh description to build the displaced mesh asset."));
		return false;
	}

	FStaticMeshSourceModel& SourceModel = GetBaseMeshSourceModel(*BaseMesh);
	
	FMeshDescription MeshDescription = *SourceModel.GetOrCacheMeshDescription();

	FMeshBuildSettings& BuildSettings = SourceModel.BuildSettings;
	FMeshDescriptionHelper MeshDescriptionHelper(&BuildSettings);
	MeshDescriptionHelper.SetupRenderMeshDescription(BaseMesh, MeshDescription);

	const FMeshSectionInfoMap BeforeBuildSectionInfoMap = BaseMesh->GetSectionInfoMap();
	const FMeshSectionInfoMap BeforeBuildOriginalSectionInfoMap = BaseMesh->GetOriginalSectionInfoMap();

	// Note: We intentionally ignore BaseMesh->NaniteSettings so we don't couple against a mesh that may
	// not ever render as Nanite directly. It is expected that anyone using a Nanite displaced mesh asset
	// will always want Nanite unless the platform, runtime, or "Disallow Nanite" on SMC prevents it.
	FMeshNaniteSettings NaniteSettings;
	NaniteSettings.bEnabled = true;

	const int32 NumSourceModels = BaseMesh->GetNumSourceModels();

	TArray<FMeshDescription> MeshDescriptions;
	MeshDescriptions.SetNum(NumSourceModels);

	Nanite::IBuilderModule::FVertexMeshData InputMeshData;

	TArray<int32> RemapVerts;
	TArray<int32> WedgeMap;

	TArray<TArray<uint32>> PerSectionIndices;
	PerSectionIndices.AddDefaulted(MeshDescription.PolygonGroups().Num());
	InputMeshData.Sections.Empty(MeshDescription.PolygonGroups().Num());

	UE::Private::StaticMeshBuilder::BuildVertexBuffer(
		BaseMesh,
		MeshDescription,
		BuildSettings,
		WedgeMap,
		InputMeshData.Sections,
		PerSectionIndices,
		InputMeshData.Vertices,
		MeshDescriptionHelper.GetOverlappingCorners(),
		RemapVerts
	);

	if (((IRequestOwner&)Owner).IsCanceled())
	{
		return false;
	}

	const uint32 NumTextureCoord = MeshDescription.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate).GetNumChannels();

	// Make sure to not keep the large WedgeMap from the input mesh around.
	WedgeMap.Empty();

	// Only the render data and vertex buffers will be used from now on unless we have more than one source models
	// This will help with memory usage for Nanite Mesh by releasing memory before doing the build
	MeshDescription.Empty();

	TArray<uint32> CombinedIndices;
	bool bNeeds32BitIndices = false;
	UE::Private::StaticMeshBuilder::BuildCombinedSectionIndices(
		PerSectionIndices,
		InputMeshData.Sections,
		InputMeshData.TriangleIndices,
		bNeeds32BitIndices
	);

	if (((IRequestOwner&)Owner).IsCanceled())
	{
		return false;
	}

	// Nanite build requires the section material indices to have already been resolved from the SectionInfoMap
	// as the indices are baked into the FMaterialTriangles.
	for (int32 SectionIndex = 0; SectionIndex < InputMeshData.Sections.Num(); SectionIndex++)
	{
		InputMeshData.Sections[SectionIndex].MaterialIndex = BaseMesh->GetSectionInfoMap().Get(0, SectionIndex).MaterialIndex;
	}
	
	TArray< int32 > MaterialIndexes;
	{
		MaterialIndexes.Reserve( InputMeshData.TriangleIndices.Num() / 3 );

		for (FStaticMeshSection& Section : InputMeshData.Sections)
		{
			if (Section.NumTriangles > 0)
			{
				Data->MeshSections.Add(Section);
			}

			for( uint32 i = 0; i < Section.NumTriangles; i++ )
				MaterialIndexes.Add( Section.MaterialIndex );
		}
	}

	// Perform displacement mapping against base mesh using supplied parameterization
	if (!DisplaceNaniteMesh(
			DisplacedMesh->Parameters,
			NumTextureCoord,
			InputMeshData.Vertices,
			InputMeshData.TriangleIndices,
			MaterialIndexes )
		)
	{
		UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Failed to build perform displacement mapping for Nanite displaced mesh asset."));
		return false;
	}

	if (((IRequestOwner&)Owner).IsCanceled())
	{
		return false;
	}

	// Compute mesh bounds after displacement has run
	// TODO: Do we need this? The base mesh bounds will not exactly match the displaced mesh bounds (but cluster bounds will be correct).
	//FBoxSphereBounds MeshBounds;
	//ComputeBoundsFromVertexList(InputMeshData.Vertices, MeshBounds);

	TArray<uint32> MeshTriangleCounts;
	MeshTriangleCounts.Add(InputMeshData.TriangleIndices.Num() / 3);

	// Pass displaced mesh over to Nanite to build the bulk data
	if (!NaniteBuilderModule.Build(
			Data->Resources,
			InputMeshData.Vertices,
			InputMeshData.TriangleIndices,
			MaterialIndexes,
			MeshTriangleCounts,
			NumTextureCoord,
			NaniteSettings)
		)
	{
		UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Failed to build Nanite for displaced mesh asset."));
		return false;
	}

	if (((IRequestOwner&)Owner).IsCanceled())
	{
		return false;
	}

	return true;
}

void FNaniteBuildAsyncCacheTask::InitResources()
{
	Async(EAsyncExecution::TaskGraphMainThread, [this]
	{
		if (UNaniteDisplacedMesh* DisplacedMesh = WeakDisplacedMesh.Get())
		{
			// Only initialize resources for the running platform
			if (Data == &DisplacedMesh->Data)
			{
				DisplacedMesh->InitResources();
			}
		}
	});
}

#endif

UNaniteDisplacedMesh::UNaniteDisplacedMesh(const FObjectInitializer& Init)
: Super(Init)
{
}

void UNaniteDisplacedMesh::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.IsFilterEditorOnly() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{
	#if WITH_EDITOR
		if (Ar.IsCooking())
		{
			FNaniteData& CookedData = CacheDerivedData(Ar.CookingTarget());
			CookedData.Resources.Serialize(Ar, this, /*bCooked*/ true);
			Ar << CookedData.MeshSections;
		}
		else
	#endif
		{
			Data.Resources.Serialize(Ar, this, /*bCooked*/ true);
			Ar << Data.MeshSections;
		}
	}
}

void UNaniteDisplacedMesh::PostLoad()
{
	if (FApp::CanEverRender())
	{
		// Only valid for cooked builds
		if (Data.Resources.PageStreamingStates.Num() > 0)
		{
			InitResources();
		}
	#if WITH_EDITOR
		else if (ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform())
		{
			BeginCacheDerivedData(RunningPlatform);
		}
	#endif
	}

	Super::PostLoad();
}

void UNaniteDisplacedMesh::BeginDestroy()
{
	Super::BeginDestroy();

	ReleaseResources();

#if WITH_EDITOR
	// Cancel any async cache and build tasks.
	CacheTasksByKeyHash.Empty();
#endif
}

bool UNaniteDisplacedMesh::IsReadyForFinishDestroy()
{
	if (!Super::IsReadyForFinishDestroy())
	{
		return false;
	}

	return ReleaseResourcesFence.IsFenceComplete();
}

void UNaniteDisplacedMesh::InitResources()
{
	if (!FApp::CanEverRender())
	{
		return;
	}

	check(!bIsInitialized);

	Data.Resources.InitResources(this);

	bIsInitialized = true;
}

void UNaniteDisplacedMesh::ReleaseResources()
{
	if (!bIsInitialized)
	{
		return;
	}

	if (Data.Resources.ReleaseResources())
	{
		// Make sure the renderer is done processing the command,
		// and done using the Nanite resources before we overwrite the data.
		ReleaseResourcesFence.BeginFence();
	}

	bIsInitialized = false;
}

bool UNaniteDisplacedMesh::HasValidNaniteData() const
{
	return bIsInitialized && Data.Resources.PageStreamingStates.Num() > 0;
}

#if WITH_EDITOR

void UNaniteDisplacedMesh::PreEditChange(FProperty* PropertyAboutToChange)
{
	// Cancel any async cache and build tasks.
	CacheTasksByKeyHash.Empty();

	// Make sure the GPU is no longer referencing the current Nanite resource data.
	ReleaseResources();
	ReleaseResourcesFence.Wait();
	Data.Resources = {};
	Data.MeshSections.Empty();

	Super::PreEditChange(PropertyAboutToChange);
}

void UNaniteDisplacedMesh::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// TODO: Add delegates for begin and end build events to safely reload scene proxies, etc.

	// Synchronously build the new data. This calls InitResources.
	ITargetPlatform* RunningPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	CacheDerivedData(RunningPlatform);

	NotifyOnRebuild();
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
	// and drop their pointers to the data.
	CacheTasksByKeyHash.Empty();
	DataByPlatformKeyHash.Empty();
	Super::ClearAllCachedCookedPlatformData();
}

void UNaniteDisplacedMesh::RegisterOnRebuild(const FOnRebuild& Delegate)
{
	OnRebuild.Add(Delegate);
}

void UNaniteDisplacedMesh::UnregisterOnRebuild(void* Unregister)
{
	OnRebuild.RemoveAll(Unregister);
}

void UNaniteDisplacedMesh::NotifyOnRebuild()
{
	OnRebuild.Broadcast();
}

FIoHash UNaniteDisplacedMesh::CreateDerivedDataKeyHash(const ITargetPlatform* TargetPlatform)
{
	FMemoryHasherBlake3 Writer;

	FGuid DisplacedMeshVersionGuid(0xDDA2ED11, 0x35AE4A11, 0xB02D0B33, 0xE7CFF4F8);
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

	Writer << Parameters.DiceRate;

	for( auto& DisplacementMap : Parameters.DisplacementMaps )
	{
		if (IsValid(DisplacementMap.Texture))
		{
			FGuid TextureId = DisplacementMap.Texture->Source.GetId();
			Writer << TextureId;
		}

		Writer << DisplacementMap.Magnitude;
		Writer << DisplacementMap.Center;
	}

	return Writer.Finalize();
}

FIoHash UNaniteDisplacedMesh::BeginCacheDerivedData(const ITargetPlatform* TargetPlatform)
{
	const FIoHash KeyHash = CreateDerivedDataKeyHash(TargetPlatform);

	if (DataKeyHash == KeyHash || DataByPlatformKeyHash.Contains(KeyHash))
	{
		return KeyHash;
	}

	FNaniteData* TargetData = nullptr;
	if (TargetPlatform->IsRunningPlatform())
	{
		DataKeyHash = KeyHash;
		TargetData = &Data;
	}
	else
	{
		TargetData = DataByPlatformKeyHash.Emplace(KeyHash, MakeUnique<FNaniteData>()).Get();
	}

	CacheTasksByKeyHash.Emplace(KeyHash, MakePimpl<FNaniteBuildAsyncCacheTask>(KeyHash, TargetData, *this, TargetPlatform));
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

FNaniteData& UNaniteDisplacedMesh::CacheDerivedData(const ITargetPlatform* TargetPlatform)
{
	const FIoHash KeyHash = BeginCacheDerivedData(TargetPlatform);
	EndCacheDerivedData(KeyHash);
	return DataKeyHash == KeyHash ? Data : *DataByPlatformKeyHash[KeyHash];
}

#endif // WITH_EDITOR
