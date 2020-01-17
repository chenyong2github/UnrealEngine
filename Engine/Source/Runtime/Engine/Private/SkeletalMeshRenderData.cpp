// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"

#if WITH_EDITOR
#include "ProfilingDebugging/CookStats.h"
#include "DerivedDataCacheInterface.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "PlatformInfo.h"
#include "IMeshBuilderModule.h"
#include "EngineUtils.h"


#if ENABLE_COOK_STATS
namespace SkeletalMeshCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("SkeletalMesh.Usage"), TEXT(""));
	});
}
#endif

//Serialize the LODInfo and append the result to the KeySuffix to build the LODInfo part of the DDC KEY
//Note: this serializer is only used to build the mesh DDC key, no versioning is required
static void SerializeLODInfoForDDC(USkeletalMesh* SkeletalMesh, FString& KeySuffix)
{
	TArray<FSkeletalMeshLODInfo>& LODInfos = SkeletalMesh->GetLODInfoArray();
	const bool bIs16BitfloatBufferSupported = GVertexElementTypeSupport.IsSupported(VET_Half2);
	for (int32 LODIndex = 0; LODIndex < SkeletalMesh->GetLODNum(); ++LODIndex)
	{
		check(LODInfos.IsValidIndex(LODIndex));
		FSkeletalMeshLODInfo& LODInfo = LODInfos[LODIndex];
		bool bValidLODSettings = false;
		if (SkeletalMesh->LODSettings != nullptr)
		{
			const int32 NumSettings = FMath::Min(SkeletalMesh->LODSettings->GetNumberOfSettings(), SkeletalMesh->GetLODNum());
			if (LODIndex < NumSettings)
			{
				bValidLODSettings = true;
			}
		}
		const FSkeletalMeshLODGroupSettings* SkeletalMeshLODGroupSettings = bValidLODSettings ? &SkeletalMesh->LODSettings->GetSettingsForLODLevel(LODIndex) : nullptr;
		LODInfo.BuildGUID = LODInfo.ComputeDeriveDataCacheKey(SkeletalMeshLODGroupSettings);
		KeySuffix += LODInfo.BuildGUID.ToString(EGuidFormats::Digits);
	}
}

// If skeletal mesh derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define SKELETALMESH_DERIVEDDATA_VER TEXT("7208E3E18CB047589DD75CD8E66EA1D6")

static const FString& GetSkeletalMeshDerivedDataVersion()
{
	static FString CachedVersionString;
	if (CachedVersionString.IsEmpty())
	{
		CachedVersionString = FString::Printf(TEXT("%s"), SKELETALMESH_DERIVEDDATA_VER);
	}
	return CachedVersionString;
}

static FString BuildSkeletalMeshDerivedDataKey(USkeletalMesh* SkelMesh)
{
	FString KeySuffix(TEXT(""));

	if (SkelMesh->UseLegacyMeshDerivedDataKey )
	{
		//Old asset will have the same LOD settings for bUseFullPrecisionUVs. We can use the LOD 0
		const FSkeletalMeshLODInfo* BaseLODInfo = SkelMesh->GetLODInfo(0);
		bool bUseFullPrecisionUVs = BaseLODInfo ? BaseLODInfo->BuildSettings.bUseFullPrecisionUVs : false;
		KeySuffix += SkelMesh->GetImportedModel()->GetIdString();
		KeySuffix += (bUseFullPrecisionUVs || !GVertexElementTypeSupport.IsSupported(VET_Half2)) ? "1" : "0";
	}
	else
	{
		FString tmpDebugString;
		//Synchronize the user data that are part of the key
		SkelMesh->GetImportedModel()->SyncronizeLODUserSectionsData();
		tmpDebugString = SkelMesh->GetImportedModel()->GetIdString();
		KeySuffix += tmpDebugString;
		tmpDebugString = SkelMesh->GetImportedModel()->GetLODModelIdString();
		KeySuffix += tmpDebugString;
		
		//Add the max gpu bone per section
		const uint32 MaxGPUSkinBones = FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones();
		KeySuffix += FString::FromInt((int32)MaxGPUSkinBones);

		tmpDebugString = TEXT("");
		SerializeLODInfoForDDC(SkelMesh, tmpDebugString);
		KeySuffix += tmpDebugString;
	}

	KeySuffix += SkelMesh->bHasVertexColors ? "1" : "0";
	KeySuffix += SkelMesh->VertexColorGuid.ToString(EGuidFormats::Digits);

	const ITargetPlatform* TargetPlatform = GetTargetPlatformManagerRef().GetRunningTargetPlatform();
	check(TargetPlatform);
	const FName PlatformGroupName = TargetPlatform->GetPlatformInfo().PlatformGroupName;
	const FName VanillaPlatformName = TargetPlatform->GetPlatformInfo().VanillaPlatformName;

	const bool bSupportLODStreaming = SkelMesh->bSupportLODStreaming.GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName);
	if (bSupportLODStreaming)
	{
		const int32 MaxNumStreamedLODs = SkelMesh->MaxNumStreamedLODs.GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName);
		const int32 MaxNumOptionalLODs = SkelMesh->MaxNumOptionalLODs.GetValueForPlatformIdentifiers(PlatformGroupName, VanillaPlatformName);
		KeySuffix += *FString::Printf(TEXT("1%08x%08x"), MaxNumStreamedLODs, MaxNumOptionalLODs);
	}
	else
	{
		KeySuffix += TEXT("0zzzzzzzzzzzzzzzz");
	}

	return FDerivedDataCacheInterface::BuildCacheKey(
		TEXT("SKELETALMESH"),
		*GetSkeletalMeshDerivedDataVersion(),
		*KeySuffix
	);
}

void FSkeletalMeshRenderData::Cache(USkeletalMesh* Owner)
{
	check(Owner);

	if (Owner->GetOutermost()->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		// Don't cache for cooked packages, source data will not be present
		return;
	}

	check(LODRenderData.Num() == 0); // Should only be called on new, empty RenderData


	{
		COOK_STAT(auto Timer = SkeletalMeshCookStats::UsageStats.TimeSyncWork());
		int32 T0 = FPlatformTime::Cycles();
		FString DerivedDataKey = BuildSkeletalMeshDerivedDataKey(Owner);

		TArray<uint8> DerivedData;
		if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData))
		{
			COOK_STAT(Timer.AddHit(DerivedData.Num()));

			FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

			//With skeletal mesh build refactor we serialize the LODModel sections into the DDC
			//We need to store those so we do not have to rerun the reduction to make them up to date
			//with the serialize renderdata. This allow to use DDC when changing the reduction settings.
			//The old workflow has to reduce the LODModel before getting the render data DDC.
			if (!Owner->UseLegacyMeshDerivedDataKey)
			{
				FSkeletalMeshModel* SkelMeshModel = Owner->GetImportedModel();
				check(SkelMeshModel);

				int32 MorphTargetNumber = 0;
				Ar << MorphTargetNumber;
				TArray<UMorphTarget*> ToDeleteMorphTargets;
				ToDeleteMorphTargets.Append(Owner->MorphTargets);
				Owner->MorphTargets.Empty();
				//Rebuild the MorphTarget object
				//We cannot serialize directly the UMorphTarget with a FMemoryArchive. This is not supported.
				for (int32 MorphTargetIndex = 0; MorphTargetIndex < MorphTargetNumber; ++MorphTargetIndex)
				{
					FName MorphTargetName = NAME_None;
					Ar << MorphTargetName;
					UMorphTarget* MorphTarget = Cast<UMorphTarget>(StaticFindObjectFast(nullptr, Owner, MorphTargetName));
					if (!MorphTarget)
					{
						MorphTarget = NewObject<UMorphTarget>(Owner, MorphTargetName);
						check(MorphTarget);
					}
					else
					{
						ToDeleteMorphTargets.Remove(MorphTarget);
					}
					MorphTarget->MorphLODModels.Empty();
					Owner->MorphTargets.Add(MorphTarget);
					check(MorphTargetIndex == Owner->MorphTargets.Num() - 1);
					int32 MorphLODModelNumber = 0;
					Ar << MorphLODModelNumber;
					MorphTarget->MorphLODModels.AddDefaulted(MorphLODModelNumber);
					for (int32 MorphDataIndex = 0; MorphDataIndex < MorphLODModelNumber; ++MorphDataIndex)
					{
						Ar << MorphTarget->MorphLODModels[MorphDataIndex];
					}
				}
				//Rebuild the mapping and rehook the curve data
				Owner->InitMorphTargets();
				for (int32 DeleteMorphIndex = 0; DeleteMorphIndex < ToDeleteMorphTargets.Num(); ++DeleteMorphIndex)
				{
					ToDeleteMorphTargets[DeleteMorphIndex]->BaseSkelMesh = nullptr;
					ToDeleteMorphTargets[DeleteMorphIndex]->MorphLODModels.Empty();
					//Move the unused asset in the transient package and mark it pending kill
					ToDeleteMorphTargets[DeleteMorphIndex]->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
					ToDeleteMorphTargets[DeleteMorphIndex]->MarkPendingKill();
				}

				//Serialize the LODModel sections since they are dependent on the reduction
				for (int32 LODIndex = 0; LODIndex < SkelMeshModel->LODModels.Num(); LODIndex++)
				{
					FSkeletalMeshLODModel* LODModel = &(SkelMeshModel->LODModels[LODIndex]);
					int32 SectionNum = 0;
					Ar << SectionNum;
					LODModel->Sections.Reset(SectionNum);
					LODModel->Sections.AddDefaulted(SectionNum);
					for (int32 SectionIndex = 0; SectionIndex < LODModel->Sections.Num(); ++SectionIndex)
					{
						Ar << LODModel->Sections[SectionIndex];
					}
					LODModel->SyncronizeUserSectionsDataArray();
				}
			}

			Serialize(Ar, Owner);
			for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); ++LODIndex)
			{
				FSkeletalMeshLODRenderData& LODData = LODRenderData[LODIndex];
				if (LODData.bStreamedDataInlined)
				{
					break;
				}
				constexpr uint8 DummyStripFlags = 0;
				const bool bForceKeepCPUResources = FSkeletalMeshLODRenderData::ShouldForceKeepCPUResources();
				const bool bNeedsCPUAccess = FSkeletalMeshLODRenderData::ShouldKeepCPUResources(Owner, LODIndex, bForceKeepCPUResources);
				LODData.SerializeStreamedData(Ar, Owner, LODIndex, DummyStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
			}

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogSkeletalMesh, Verbose, TEXT("Skeletal Mesh found in DDC [%fms] %s"), FPlatformTime::ToMilliseconds(T1 - T0), *Owner->GetPathName());
		}
		else
		{
			UE_LOG(LogSkeletalMesh, Log, TEXT("Building Skeletal Mesh %s..."),*Owner->GetName());

			// Allocate empty entries for each LOD level in source mesh
			FSkeletalMeshModel* SkelMeshModel = Owner->GetImportedModel();
			check(SkelMeshModel);

			uint32 VertexBufferBuildFlags = Owner->GetVertexBufferFlags();

			for (int32 LODIndex = 0; LODIndex < SkelMeshModel->LODModels.Num(); LODIndex++)
			{
				FSkeletalMeshLODModel* LODModel = &(SkelMeshModel->LODModels[LODIndex]);
				FSkeletalMeshLODInfo* LODInfo = Owner->GetLODInfo(LODIndex);
				check(LODInfo);
				bool bRawDataEmpty = Owner->IsLODImportedDataEmpty(LODIndex);
				bool bRawBuildDataAvailable = Owner->IsLODImportedDataBuildAvailable(LODIndex);
				//Build the source model before the render data, if we are a purely generated LOD we do not need to be build
				IMeshBuilderModule& MeshBuilderModule = FModuleManager::Get().LoadModuleChecked<IMeshBuilderModule>("MeshBuilder");
				if (!bRawDataEmpty && bRawBuildDataAvailable)
				{
					MeshBuilderModule.BuildSkeletalMesh(Owner, LODIndex, true);
					LODModel = &(SkelMeshModel->LODModels[LODIndex]);
				}
				else
				{
					//We need to synchronize when we are generated mesh or if we have load an old asset that was not re-imported
					LODModel->SyncronizeUserSectionsDataArray();
				}

				FSkeletalMeshLODRenderData* LODData = new FSkeletalMeshLODRenderData();
				LODRenderData.Add(LODData);
				
				//Get the UVs and tangents precision build settings flag specific for this LOD index
				{
					bool bUseFullPrecisionUVs = LODInfo->BuildSettings.bUseFullPrecisionUVs;
					bool bUseHighPrecisionTangentBasis = LODInfo->BuildSettings.bUseHighPrecisionTangentBasis;
					bool bBuildAdjacencyBuffer = LODInfo->BuildSettings.bBuildAdjacencyBuffer;
					if (bUseFullPrecisionUVs || !GVertexElementTypeSupport.IsSupported(VET_Half2))
					{
						VertexBufferBuildFlags |= ESkeletalMeshVertexFlags::UseFullPrecisionUVs;
					}
					if (bUseHighPrecisionTangentBasis)
					{
						VertexBufferBuildFlags |= ESkeletalMeshVertexFlags::UseHighPrecisionTangentBasis;
					}
					if (bBuildAdjacencyBuffer)
					{
						VertexBufferBuildFlags |= ESkeletalMeshVertexFlags::BuildAdjacencyIndexBuffer;
					}
				}
				LODData->BuildFromLODModel(LODModel, VertexBufferBuildFlags);
			}

			FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
			
			//If we load an old asset we want to be sure the serialize ddc will be the same has before the skeletalmesh build refactor
			//So we do not serialize the LODModel sections.
			if (!Owner->UseLegacyMeshDerivedDataKey)
			{
				int32 MorphTargetNumber = Owner->MorphTargets.Num();
				Ar << MorphTargetNumber;
				for (int32 MorphTargetIndex = 0; MorphTargetIndex < MorphTargetNumber; ++MorphTargetIndex)
				{
					FName MorphTargetName = Owner->MorphTargets[MorphTargetIndex]->GetFName();
					Ar << MorphTargetName;
					int32 MorphLODModelNumber = Owner->MorphTargets[MorphTargetIndex]->MorphLODModels.Num();
					Ar << MorphLODModelNumber;
					for (int32 MorphIndex = 0; MorphIndex < MorphLODModelNumber; ++MorphIndex)
					{
						Ar << Owner->MorphTargets[MorphTargetIndex]->MorphLODModels[MorphIndex];
					}
				}
				//No need to serialize the morph target mapping since we will rebuild the mapping when loading a ddc

				//Serialize the LODModel sections since they are dependent on the reduction
				for (int32 LODIndex = 0; LODIndex < SkelMeshModel->LODModels.Num(); LODIndex++)
				{
					FSkeletalMeshLODModel* LODModel = &(SkelMeshModel->LODModels[LODIndex]);
					int32 SectionNum = LODModel->Sections.Num();
					Ar << SectionNum;
					for (int32 SectionIndex = 0; SectionIndex < LODModel->Sections.Num(); ++SectionIndex)
					{
						Ar << LODModel->Sections[SectionIndex];
					}
				}
			}

			//Serialize the render data
			Serialize(Ar, Owner);
			for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); ++LODIndex)
			{
				FSkeletalMeshLODRenderData& LODData = LODRenderData[LODIndex];
				if (LODData.bStreamedDataInlined)
				{
					break;
				}
				const uint8 LODStripFlags = FSkeletalMeshLODRenderData::GenerateClassStripFlags(Ar, Owner, LODIndex);
				const bool bForceKeepCPUResources = FSkeletalMeshLODRenderData::ShouldForceKeepCPUResources();
				const bool bNeedsCPUAccess = FSkeletalMeshLODRenderData::ShouldKeepCPUResources(Owner, LODIndex, bForceKeepCPUResources);
				LODData.SerializeStreamedData(Ar, Owner, LODIndex, LODStripFlags, bNeedsCPUAccess, bForceKeepCPUResources);
			}
			GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData);

			int32 T1 = FPlatformTime::Cycles();
			UE_LOG(LogSkeletalMesh, Log, TEXT("Built Skeletal Mesh [%.2fs] %s"), FPlatformTime::ToMilliseconds(T1 - T0) / 1000.0f, *Owner->GetPathName());
			COOK_STAT(Timer.AddMiss(DerivedData.Num()));
		}
	}
}

void FSkeletalMeshRenderData::SyncUVChannelData(const TArray<FSkeletalMaterial>& ObjectData)
{
	TUniquePtr< TArray<FMeshUVChannelInfo> > UpdateData = MakeUnique< TArray<FMeshUVChannelInfo> >();
	UpdateData->Empty(ObjectData.Num());

	for (const FSkeletalMaterial& SkeletalMaterial : ObjectData)
	{
		UpdateData->Add(SkeletalMaterial.UVChannelData);
	}

	ENQUEUE_RENDER_COMMAND(SyncUVChannelData)([this, UpdateData = MoveTemp(UpdateData)](FRHICommandListImmediate& RHICmdList)
	{
		FMemory::Memswap(&UVChannelDataPerMaterial, UpdateData.Get(), sizeof(TArray<FMeshUVChannelInfo>));
	});
}

#endif // WITH_EDITOR

FSkeletalMeshRenderData::FSkeletalMeshRenderData()
	: bReadyForStreaming(false)
	, NumInlinedLODs(0)
	, NumOptionalLODs(0)
	, CurrentFirstLODIdx(0)
	, PendingFirstLODIdx(0)
	, bInitialized(false)
{}

void FSkeletalMeshRenderData::Serialize(FArchive& Ar, USkeletalMesh* Owner)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FSkeletalMeshRenderData::Serialize"), STAT_SkeletalMeshRenderData_Serialize, STATGROUP_LoadTime);

	LODRenderData.Serialize(Ar, Owner);

#if WITH_EDITOR
	if (Ar.IsSaving())
	{
		NumInlinedLODs = 0;
		NumOptionalLODs = 0;
		for (int32 Idx = LODRenderData.Num() - 1; Idx >= 0; --Idx)
		{
			if (LODRenderData[Idx].bStreamedDataInlined)
			{
				++NumInlinedLODs;
			}
			if (LODRenderData[Idx].bIsLODOptional)
			{
				++NumOptionalLODs;
			}
		}
	}
#endif
	Ar << NumInlinedLODs << NumOptionalLODs;
	
	CurrentFirstLODIdx = LODRenderData.Num() - NumInlinedLODs;
	PendingFirstLODIdx = CurrentFirstLODIdx;
	Owner->SetCachedNumResidentLODs(NumInlinedLODs);
}

void FSkeletalMeshRenderData::InitResources(bool bNeedsVertexColors, TArray<UMorphTarget*>& InMorphTargets, USkeletalMesh* Owner)
{
	if (!bInitialized)
	{
		// initialize resources for each lod
		for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); LODIndex++)
		{
			FSkeletalMeshLODRenderData& RenderData = LODRenderData[LODIndex];

			if(RenderData.GetNumVertices() > 0)
			{
				RenderData.InitResources(bNeedsVertexColors, LODIndex, InMorphTargets, Owner);
			}
		}

		ENQUEUE_RENDER_COMMAND(CmdSetSkeletalMeshReadyForStreaming)(
			[this, Owner](FRHICommandListImmediate&)
		{
			bReadyForStreaming = true;
			Owner->SetCachedReadyForStreaming(true);
		});

		bInitialized = true;
	}
}

void FSkeletalMeshRenderData::ReleaseResources()
{
	if (bInitialized)
	{
		// release resources for each lod
		for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); LODIndex++)
		{
			LODRenderData[LODIndex].ReleaseResources();
		}
		bInitialized = false;
	}
}

bool FSkeletalMeshRenderData::HasExtraBoneInfluences() const
{
	for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); ++LODIndex)
	{
		const FSkeletalMeshLODRenderData& Data = LODRenderData[LODIndex];
		if (Data.DoesVertexBufferHaveExtraBoneInfluences())
		{
			return true;
		}
	}

	return false;
}

bool FSkeletalMeshRenderData::RequiresCPUSkinning(ERHIFeatureLevel::Type FeatureLevel) const
{
	const int32 MaxGPUSkinBones = FMath::Min(GetFeatureLevelMaxNumberOfBones(FeatureLevel), FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones());
	const int32 MaxBonesPerChunk = GetMaxBonesPerSection();
	// Do CPU skinning if we need too many bones per chunk, or if we have too many influences per vertex on lower end
	return (MaxBonesPerChunk > MaxGPUSkinBones) || (HasExtraBoneInfluences() && FeatureLevel < ERHIFeatureLevel::ES3_1);
}

void FSkeletalMeshRenderData::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); ++LODIndex)
	{
		const FSkeletalMeshLODRenderData& RenderData = LODRenderData[LODIndex];
		RenderData.GetResourceSizeEx(CumulativeResourceSize);
	}
}

int32 FSkeletalMeshRenderData::GetMaxBonesPerSection() const
{
	int32 MaxBonesPerSection = 0;
	for (int32 LODIndex = 0; LODIndex < LODRenderData.Num(); ++LODIndex)
	{
		const FSkeletalMeshLODRenderData& RenderData = LODRenderData[LODIndex];
		for (int32 SectionIndex = 0; SectionIndex < RenderData.RenderSections.Num(); ++SectionIndex)
		{
			MaxBonesPerSection = FMath::Max<int32>(MaxBonesPerSection, RenderData.RenderSections[SectionIndex].BoneMap.Num());
		}
	}
	return MaxBonesPerSection;
}
