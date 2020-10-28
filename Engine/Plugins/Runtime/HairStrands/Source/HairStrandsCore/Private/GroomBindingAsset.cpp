// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomBindingAsset.h"
#include "EngineUtils.h"
#include "GroomAsset.h"
#include "GroomBindingBuilder.h"

#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheInterface.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "GroomComponent.h"
#endif

/////////////////////////////////////////////////////////////////////////////////////////

FArchive& operator<<(FArchive& Ar, FHairStrandsRootData& RootData)
{
	RootData.Serialize(Ar);
	return Ar;
}

FArchive& operator<<(FArchive& Ar, UGroomBindingAsset::FHairGroupData& GroupData)
{
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
	GroupData.SimRootData.Serialize(Ar);
	GroupData.RenRootData.Serialize(Ar);
	if (Ar.CustomVer(FAnimObjectVersion::GUID) >= FAnimObjectVersion::SerializeHairBindingAsset)
	{
		Ar << GroupData.CardsRootData;
	}
	return Ar;
}

void UGroomBindingAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
#if WITH_EDITOR
	if (Ar.CustomVer(FAnimObjectVersion::GUID) < FAnimObjectVersion::GroomBindingSerialization || Ar.IsCooking())
#endif
	{
		Ar << HairGroupDatas;
		bIsValid = true;
	}
}

void UGroomBindingAsset::InitResource()
{
	LLM_SCOPE(ELLMTag::Meshes) // This should be a Groom LLM tag, but there is no LLM tag bit left

	for (FHairGroupData& Data : HairGroupDatas)
	{
		const int32 GroupIndex = HairGroupResources.Num();
		FHairGroupResource& Resource = HairGroupResources.AddDefaulted_GetRef();

		// Guides
		Resource.SimRootResources = nullptr;
		if (Data.SimRootData.IsValid())
		{
			Resource.SimRootResources = new FHairStrandsRestRootResource(Data.SimRootData);
			BeginInitResource(Resource.SimRootResources);
		}

		// Strands
		Resource.RenRootResources = nullptr;
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Strands) && Data.RenRootData.IsValid())
		{
			Resource.RenRootResources = new FHairStrandsRestRootResource(Data.RenRootData);
			BeginInitResource(Resource.RenRootResources);
		}

		// Cards
		if (IsHairStrandsEnabled(EHairStrandsShaderType::Cards))
		{
			const uint32 CardsLODCount = Data.CardsRootData.Num();
			Resource.CardsRootResources.SetNum(CardsLODCount);
			for (uint32 CardsLODIt=0; CardsLODIt<CardsLODCount; ++CardsLODIt)
			{
				Resource.CardsRootResources[CardsLODIt] = nullptr;
				if (Data.CardsRootData[CardsLODIt].IsValid())
				{
					Resource.CardsRootResources[CardsLODIt] = new FHairStrandsRestRootResource(Data.CardsRootData[CardsLODIt]);
					BeginInitResource(Resource.CardsRootResources[CardsLODIt]);
				}
			}
		}
	}
}

void UGroomBindingAsset::UpdateResource()
{
	for (FHairGroupResource& Resource : HairGroupResources)
	{
		if (Resource.SimRootResources)
		{
			BeginUpdateResourceRHI(Resource.SimRootResources);
		}

		if (Resource.RenRootResources)
		{
			BeginUpdateResourceRHI(Resource.RenRootResources);
		}

		for (FHairStrandsRestRootResource* CardsRootResource : Resource.CardsRootResources)
		{
			if (CardsRootResource)
			{
				BeginUpdateResourceRHI(CardsRootResource);
			}
		}
	}
}

void UGroomBindingAsset::ReleaseResource()
{
	// Delay destruction to insure that the rendering thread is done with all resources usage
	if (HairGroupResources.Num() > 0)
	{
		for (FHairGroupResource& Resource : HairGroupResources)
		{
			FHairStrandsRestRootResource* InSimRootResources = Resource.SimRootResources;
			FHairStrandsRestRootResource* InRenRootResources = Resource.RenRootResources;
			ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
				[InSimRootResources, InRenRootResources](FRHICommandList& RHICmdList)
			{
				if (InSimRootResources)
				{
					InSimRootResources->ReleaseResource();
					delete InSimRootResources;
				}
				if (InRenRootResources)
				{
					InRenRootResources->ReleaseResource();
					delete InRenRootResources;
				}
			});
			Resource.SimRootResources = nullptr;
			Resource.RenRootResources = nullptr;

			for (FHairStrandsRestRootResource*& InCardsRootResources : Resource.CardsRootResources)
			{
				ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
					[InCardsRootResources](FRHICommandList& RHICmdList)
					{
						if (InCardsRootResources)
						{
							InCardsRootResources->ReleaseResource();
							delete InCardsRootResources;
						}
					});
				InCardsRootResources = nullptr;
			}
		}
		HairGroupResources.Empty();
	}

	// Process resources to be deleted (should happen only in editor)
	FHairGroupResource ResourceToDelete;
	while (HairGroupResourcesToDelete.Dequeue(ResourceToDelete))
	{
		FHairStrandsRestRootResource* InSimRootResources = ResourceToDelete.SimRootResources;
		FHairStrandsRestRootResource* InRenRootResources = ResourceToDelete.RenRootResources;
		ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
			[InSimRootResources, InRenRootResources](FRHICommandList& RHICmdList)
		{
			if (InSimRootResources)
			{
				InSimRootResources->ReleaseResource();
				delete InSimRootResources;
			}
			if (InRenRootResources)
			{
				InRenRootResources->ReleaseResource();
				delete InRenRootResources;
			}
		});
		//	#hair_todo: release cards root resources
	}
}

void UGroomBindingAsset::Reset()
{
	ReleaseResource();
	for (FHairGroupData& Data : HairGroupDatas)
	{
		Data.SimRootData.Reset();
		Data.RenRootData.Reset();

		for (FHairStrandsRootData& CardsRootData : Data.CardsRootData)
		{
			CardsRootData.Reset();
		}
	}

	bIsValid = false;
}

#if WITH_EDITORONLY_DATA
void UGroomBindingAsset::InvalidateBinding(class USkeletalMesh*)
{
	CacheDerivedDatas();
}

void UGroomBindingAsset::InvalidateBinding()
{
	CacheDerivedDatas();
}

#endif

void UGroomBindingAsset::PostLoad()
{
	LLM_SCOPE(ELLMTag::Meshes) // This should be a Groom LLM tag, but there is no LLM tag bit left

	Super::PostLoad();

	if (Groom)
	{
		// Make sure that the asset initialized its resources first since the component needs them to initialize its own resources
		Groom->ConditionalPostLoad();

	#if WITH_EDITOR
		CacheDerivedDatas();

		// Sanity check. This function will report back warnings/issues into the log for user.
		UGroomBindingAsset::IsCompatible(Groom, this, true);

		if (TargetSkeletalMesh)
		{
			TargetSkeletalMesh->OnPostMeshCached().AddUObject(this, &UGroomBindingAsset::InvalidateBinding);
			bRegisterTargetMeshCallback = true;
		}

		if (SourceSkeletalMesh)
		{
			SourceSkeletalMesh->OnPostMeshCached().AddUObject(this, &UGroomBindingAsset::InvalidateBinding);
			bRegisterSourceMeshCallback = true;
		}

		if (Groom)
		{
			Groom->GetOnGroomAssetChanged().AddUObject(this, &UGroomBindingAsset::InvalidateBinding);
			bRegisterGroomAssetCallback = true;
		}
	#endif
	}

	if (!IsTemplate())
	{
		InitResource();
	}
}

void UGroomBindingAsset::PreSave(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	while (QueryStatus == EQueryStatus::Submitted)
	{
		FPlatformProcess::Sleep(1);
	}
#endif
	Super::PreSave(TargetPlatform);
#if WITH_EDITOR
	OnGroomBindingAssetChanged.Broadcast();
#endif
}

void UGroomBindingAsset::PostSaveRoot(bool bCleanupIsRequired)
{
	Super::PostSaveRoot(bCleanupIsRequired);
#if WITH_EDITOR
	OnGroomBindingAssetChanged.Broadcast();
#endif
}

void UGroomBindingAsset::BeginDestroy()
{
	ReleaseResource();
	Super::BeginDestroy();

#if WITH_EDITOR
	if (TargetSkeletalMesh && bRegisterTargetMeshCallback)
	{
		TargetSkeletalMesh->OnPostMeshCached().RemoveAll(this);
		bRegisterTargetMeshCallback = false;
	}

	if (SourceSkeletalMesh && bRegisterSourceMeshCallback)
	{
		SourceSkeletalMesh->OnPostMeshCached().RemoveAll(this);
		bRegisterSourceMeshCallback = false;
	}

	if (Groom && bRegisterGroomAssetCallback)
	{
		Groom->GetOnGroomAssetChanged().RemoveAll(this);
		bRegisterGroomAssetCallback = false;
	}
#endif
}

bool UGroomBindingAsset::IsCompatible(const USkeletalMesh* InSkeletalMesh, const UGroomBindingAsset* InBinding, bool bIssueWarning)
{
	if (InBinding && InSkeletalMesh && IsHairStrandsBindingEnable())
	{
		if (!InBinding->TargetSkeletalMesh)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not have a target skeletal mesh. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}
		
		// TODO: need something better to assess that skeletal meshes match. In the mean time, string comparison. 
		// Since they can be several instances of a skeletalMesh asset (??), a numerical suffix is sometime added to the name (e.g., SkeletalName_0).
		// This is why we are using substring comparison.
		//if (InSkeletalMesh->GetPrimaryAssetId() != InBinding->TargetSkeletalMesh->GetPrimaryAssetId())
		if (!InSkeletalMesh->GetName().Contains(InBinding->TargetSkeletalMesh->GetName()))
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%s) does not reference the same skeletal asset (BindingAsset's skeletal:%s vs. Attached skeletal:%s). The binding asset will not be used."),
					*InBinding->GetName(),
					*InBinding->TargetSkeletalMesh->GetName(),
					*InSkeletalMesh->GetName());
			}
			return false;
		}

		for (const FHairGroupResource& Resource : InBinding->HairGroupResources)
		{
			if ((Resource.RenRootResources && Resource.SimRootResources) && 
				(InSkeletalMesh->GetLODNum() != Resource.RenRootResources->RootData.MeshProjectionLODs.Num() ||
				InSkeletalMesh->GetLODNum() != Resource.SimRootResources->RootData.MeshProjectionLODs.Num()))
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%s) does not have the same have LOD count (LOD render:%d, LOD sim:%d) than the skeletal mesh (%s, LOD:%d). The binding asset will not be used."),
						*InBinding->GetName(),
						Resource.RenRootResources->RootData.MeshProjectionLODs.Num(),
						Resource.SimRootResources->RootData.MeshProjectionLODs.Num(),
						*InSkeletalMesh->GetName(),
						InSkeletalMesh->GetLODNum());
				}
				return false;
			}
		}
	}

	return true;
}

bool UGroomBindingAsset::IsCompatible(const UGroomAsset* InGroom, const UGroomBindingAsset* InBinding, bool bIssueWarning)
{
	if (InBinding && InGroom && IsHairStrandsBindingEnable())
	{
		if (InBinding->Groom && !InBinding->Groom->IsValid())
		{
			// The groom could be invalid if it's still being loaded asynchronously
			return false;
		}

		if (!InBinding->Groom)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not reference a groom. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}

		if (InGroom->GetPrimaryAssetId() != InBinding->Groom->GetPrimaryAssetId())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The Groom binding (%) does not reference the same groom asset (BindingAsset's groom:%s vs. Groom:%s). The binding asset will not be used."), 
				*InBinding->GetName(),
				*InBinding->Groom->GetName(),
				*InGroom->GetName());
			return false;
		}

		const uint32 GroupCount = InGroom->GetNumHairGroups();
		if (GroupCount != InBinding->GroupInfos.Num())
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same number of groups (%d vs. %d) than the groom (%s). The binding asset will not be used."),
					*InBinding->GetName(),
					GroupCount,
					InBinding->GroupInfos.Num(),
					*InGroom->GetName());
			}
			return false;
		}

		for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
		{
			{
				const uint32 GroomCount = InGroom->HairGroupsData[GroupIt].Guides.Data.GetNumCurves();
				const uint32 BindingCount = InBinding->GroupInfos[GroupIt].SimRootCount;

				if (GroomCount != BindingCount)
				{
					if (bIssueWarning)
					{
						UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same guides in group %d (%d vs. %d) than the groom (%s). The binding asset will not be used."),
							*InBinding->GetName(),
							GroupIt,
							GroomCount,
							BindingCount,
							*InGroom->GetName());
					}
					return false;
				}
			}

			{
				const uint32 GroomCount = InGroom->HairGroupsData[GroupIt].Strands.Data.GetNumCurves();
				const uint32 BindingCount = InBinding->GroupInfos[GroupIt].RenRootCount;

				if (GroomCount != BindingCount)
				{
					if (bIssueWarning)
					{
						UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (%s) does not contains the same curves in group %d (%d vs. %d) than the groom (%s). The binding asset will not be used."),
							*InBinding->GetName(),
							GroupIt,
							GroomCount,
							BindingCount,
							*InGroom->GetName());
					}
					return false;
				}
			}
		}
	}
	return true;
}

bool UGroomBindingAsset::IsBindingAssetValid(const UGroomBindingAsset* InBinding, bool bIsBindingReloading, bool bIssueWarning)
{
	if (InBinding)
	{
		if (!InBinding->IsValid())
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) is invalid. It failed to load or build. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}

		if (!InBinding->Groom)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) does not reference a groom. Falling back onto non-binding version."), *InBinding->GetName());
			}
			return false;
		}

		if (const UPackage* Package = InBinding->GetOutermost())
		{
			if (Package->IsDirty() && !bIsBindingReloading)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The binding asset (%s) is not saved and will be considered as invalid. Falling back onto non-binding version."), *InBinding->GetName());
				}
				return false;
			}
		}

		if (InBinding->GroupInfos.Num() == 0)
		{
			if (bIssueWarning)
			{
				UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) does not contain any groups. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
			}
			return false;
		}

		for (const FGoomBindingGroupInfo& Info : InBinding->GroupInfos)
		{
			if (Info.SimRootCount == 0)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) has group with 0 guides. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
				}
				return false;
			}

			if (Info.RenRootCount == 0)
			{
				if (bIssueWarning)
				{
					UE_LOG(LogHairStrands, Warning, TEXT("[Groom] The GroomBinding asset (Groom:%s) has group with 0 curves. It is invalid and can't be assigned. The binding asset will not be used."), *InBinding->Groom->GetName());
				}
				return false;
			}
		}
	}
	return true;
}

#if WITH_EDITOR

void UGroomBindingAsset::Build()
{
	if (Groom && TargetSkeletalMesh)
	{
		OnGroomBindingAssetChanged.Broadcast();
		Reset();
		CacheDerivedDatas();
	}
}

void UGroomBindingAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateResource();
	OnGroomBindingAssetChanged.Broadcast();

	CacheDerivedDatas();
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA

// If groom binding derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define GROOM_BINDING_DERIVED_DATA_VERSION TEXT("C1865BEF29E643BB9BC7DC87E8C7A512")

namespace GroomBindingDerivedDataCacheUtils
{
	const FString& GetGroomBindingDerivedDataVersion()
	{
		static FString CachedVersionString(GROOM_BINDING_DERIVED_DATA_VERSION);
		return CachedVersionString;
	}

	FString BuildGroomBindingDerivedDataKey(const FString& KeySuffix)
	{
		return FDerivedDataCacheInterface::BuildCacheKey(*(TEXT("GROOM_BINDING_V") + FGroomBindingBuilder::GetVersion() + TEXT("_")), *GetGroomBindingDerivedDataVersion(), *KeySuffix);
	}
}

FString UGroomBindingAsset::BuildDerivedDataKeySuffix(USkeletalMesh* InSource, USkeletalMesh* InTarget, UGroomAsset* InGroom, uint32 InNumInterpolationPoints)
{
	FString SourceKey = InSource ? InSource->GetDerivedDataKey() : FString();
	FString TargetKey = InTarget ? InTarget->GetDerivedDataKey() : FString();
	FString GroomKey  = InGroom  ? InGroom->GetDerivedDataKey()  : FString();
	FString PointKey  = FString::FromInt(InNumInterpolationPoints);
	uint32 KeyLength  = SourceKey.Len() + TargetKey.Len() + GroomKey.Len() + PointKey.Len();

	FString KeySuffix;
	KeySuffix.Reserve(KeyLength);
	KeySuffix = SourceKey + TargetKey + GroomKey + PointKey;
	return KeySuffix;
}

void UGroomBindingAsset::CacheDerivedDatas()
{
	if (!Groom->IsValid())
	{
		// The groom could be invalid if it's still being loaded asynchronously
		return;
	}

	// List all the components which will need to be recreated to get the new binding information
	const FString KeySuffix = BuildDerivedDataKeySuffix(SourceSkeletalMesh, TargetSkeletalMesh, Groom, NumInterpolationPoints);
	const FString DerivedDataKey = GroomBindingDerivedDataCacheUtils::BuildGroomBindingDerivedDataKey(KeySuffix);

	if (DerivedDataKey != CachedDerivedDataKey)
	{
		FGroomComponentRecreateRenderStateContext RecreateRenderContext(Groom);

		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData, GetPathName()))
		{
			UE_LOG(LogHairStrands, Log, TEXT("[GroomBinding/DDC] Found (GroomiBinding:%s)."), *GetName());

			FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

			int64 UncompressedSize = 0;
			Ar << UncompressedSize;

			uint8* DecompressionBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(UncompressedSize));
			Ar.SerializeCompressed(DecompressionBuffer, 0, NAME_Zlib);

			FLargeMemoryReader LargeMemReader(DecompressionBuffer, UncompressedSize, ELargeMemoryReaderFlags::Persistent | ELargeMemoryReaderFlags::TakeOwnership);
			LargeMemReader << HairGroupDatas;

			bIsValid = true;
		}
		else
		{
			UE_LOG(LogHairStrands, Log, TEXT("[GroomBinding/DDC] Not found (GroomiBinding:%s)."), *GetName());

			// Build groom binding data
			bIsValid = FGroomBindingBuilder::BuildBinding(this, false, false);
			if (bIsValid)
			{
				// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
				FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
				LargeMemWriter << HairGroupDatas;

				int64 UncompressedSize = LargeMemWriter.TotalSize();

				// Then the content of the LargeMemWriter is compressed into a MemoryWriter
				// Compression ratio can reach about 5:2 depending on the data
				{
					FMemoryWriter CompressedArchive(DerivedData, true);

					CompressedArchive << UncompressedSize; // needed for allocating decompression buffer
					CompressedArchive.SerializeCompressed(LargeMemWriter.GetData(), UncompressedSize, NAME_Zlib);

					GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData, GetPathName());
				}
			}
		}

		// Patch hair group info if it does not match the DDC-read/deserialized data
		const uint32 GroupCount = HairGroupDatas.Num();
		if (GroupInfos.Num() != GroupCount)
		{
			GroupInfos.SetNum(GroupCount);
		}
		for (uint32 GroupIt=0; GroupIt< GroupCount; ++GroupIt)
		{
			FGoomBindingGroupInfo& Info = GroupInfos[GroupIt];
			const UGroomBindingAsset::FHairGroupData& Data = HairGroupDatas[GroupIt];
			{
				Info.SimRootCount = Data.SimRootData.RootCount;
				Info.SimLODCount  = Data.SimRootData.MeshProjectionLODs.Num();
				Info.RenRootCount = Data.RenRootData.RootCount;
				Info.RenLODCount  = Data.RenRootData.MeshProjectionLODs.Num();
			}
		}

		if (bIsValid)
		{
			CachedDerivedDataKey = DerivedDataKey;
			InitResource();
		}
		else
		{
			UE_LOG(LogHairStrands, Error, TEXT("[Groom] The binding asset (%s) couldn't be built. This binding asset won't be used."), *GetName());
		}
	}
}

#endif