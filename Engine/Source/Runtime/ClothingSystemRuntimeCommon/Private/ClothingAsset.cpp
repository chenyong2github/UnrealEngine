// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothingAsset.h"
#include "ClothingAssetCustomVersion.h"
#include "ClothPhysicalMeshData.h"
#include "ClothConfig.h"

#include "Utils/ClothingMeshUtils.h"
#include "Features/IModularFeatures.h"

#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#endif

#include "PhysicsEngine/PhysicsAsset.h"

#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"

#include "Components/SkeletalMeshComponent.h"

#include "ClothingSimulationInteractor.h"
#include "ComponentReregisterContext.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PhysicsObjectVersion.h"

#include "GPUSkinPublicDefs.h"
#include "GPUSkinVertexFactory.h"

DEFINE_LOG_CATEGORY(LogClothingAsset)
#define LOCTEXT_NAMESPACE "ClothingAsset"

//==============================================================================
// ClothingAssetUtils
//==============================================================================

void ClothingAssetUtils::GetMeshClothingAssetBindings(
	USkeletalMesh* InSkelMesh, 
	TArray<FClothingAssetMeshBinding>& OutBindings)
{
	OutBindings.Empty();

	if(!InSkelMesh)
	{
		return;
	}
#if WITH_EDITORONLY_DATA
	if (InSkelMesh->GetImportedModel())
	{
		int32 LODNum = InSkelMesh->GetImportedModel()->LODModels.Num();
		for (int32 LODIndex = 0; LODIndex < LODNum; ++LODIndex)
		{
			if (InSkelMesh->GetImportedModel()->LODModels[LODIndex].HasClothData())
			{
				TArray<FClothingAssetMeshBinding> LodBindings;
				GetMeshClothingAssetBindings(InSkelMesh, LodBindings, LODIndex);
				OutBindings.Append(LodBindings);
			}
		}
		if (OutBindings.Num() > 0)
		{
			return;
		}
	}
#endif

	//Fallback on render data
	if (FSkeletalMeshRenderData* Resource = InSkelMesh->GetResourceForRendering())
	{
		const int32 NumLods = Resource->LODRenderData.Num();

		for (int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
		{
			TArray<FClothingAssetMeshBinding> LodBindings;
			GetMeshClothingAssetBindings(InSkelMesh, LodBindings, LodIndex);

			OutBindings.Append(LodBindings);
		}
	}
}

void ClothingAssetUtils::GetMeshClothingAssetBindings(
	USkeletalMesh* InSkelMesh, 
	TArray<FClothingAssetMeshBinding>& OutBindings, 
	int32 InLodIndex)
{
	OutBindings.Empty();

	if(!InSkelMesh)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (InSkelMesh->GetImportedModel())
	{
		int32 LODNum = InSkelMesh->GetImportedModel()->LODModels.Num();
		if (InSkelMesh->GetImportedModel()->LODModels[InLodIndex].HasClothData())
		{
			TArray<FClothingAssetMeshBinding> LodBindings;
			int32 SectionNum = InSkelMesh->GetImportedModel()->LODModels[InLodIndex].Sections.Num();
			for (int32 SectionIndex = 0; SectionIndex < SectionNum; ++SectionIndex)
			{
				const FSkelMeshSection& Section = InSkelMesh->GetImportedModel()->LODModels[InLodIndex].Sections[SectionIndex];
				if (Section.HasClothingData())
				{
					UClothingAssetBase* ClothingAsset = InSkelMesh->GetClothingAsset(Section.ClothingData.AssetGuid);
					FClothingAssetMeshBinding ClothBinding;
					ClothBinding.Asset = Cast<UClothingAssetCommon>(ClothingAsset);
					ClothBinding.AssetInternalLodIndex = Section.ClothingData.AssetLodIndex;// InSkelMesh->GetClothingAssetIndex(Section.ClothingData.AssetGuid);
					check(ClothBinding.AssetInternalLodIndex == Section.ClothingData.AssetLodIndex);
					ClothBinding.LODIndex = InLodIndex;
					ClothBinding.SectionIndex = SectionIndex;
					OutBindings.Add(ClothBinding);
				}
			}
		}

		if (OutBindings.Num() > 0)
		{
			return;
		}
	}
#endif

	//Fallback on render data
	if(FSkeletalMeshRenderData* Resource = InSkelMesh->GetResourceForRendering())
	{
		if(Resource->LODRenderData.IsValidIndex(InLodIndex))
		{
			FSkeletalMeshLODRenderData& LodData = Resource->LODRenderData[InLodIndex];

			const int32 NumSections = LodData.RenderSections.Num();

			for(int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
			{
				FSkelMeshRenderSection& Section = LodData.RenderSections[SectionIndex];

				if(Section.HasClothingData())
				{
					UClothingAssetCommon* SectionAsset = Cast<UClothingAssetCommon>(InSkelMesh->GetSectionClothingAsset(InLodIndex, SectionIndex));

					if(SectionAsset)
					{
						// This is the original section of a clothing section pair
						OutBindings.AddDefaulted();
						FClothingAssetMeshBinding& Binding = OutBindings.Last();

						Binding.Asset = SectionAsset;
						Binding.LODIndex = InLodIndex;
						Binding.SectionIndex = SectionIndex;
						Binding.AssetInternalLodIndex = Section.ClothingData.AssetLodIndex;
					}
				}
			}
		}
	}
}

#if WITH_EDITOR
void ClothingAssetUtils::ClearSectionClothingData(FSkelMeshSection& InSection)
{
	InSection.ClothingData.AssetGuid = FGuid();
	InSection.ClothingData.AssetLodIndex = INDEX_NONE;
	InSection.CorrespondClothAssetIndex = INDEX_NONE;

	InSection.ClothMappingData.Empty();
}
#endif

//==============================================================================
// UClothingAssetCommon
//==============================================================================

UClothingAssetCommon::UClothingAssetCommon(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PhysicsAsset(nullptr)
#if WITH_EDITORONLY_DATA
	, ClothSimConfig_DEPRECATED(nullptr)
	, ChaosClothSimConfig_DEPRECATED(nullptr)
#endif
	, ReferenceBoneIndex(0)
	, CustomData(nullptr)
{
}

#if WITH_EDITOR

void Warn(const FText& Error)
{
	FNotificationInfo Info(Error);
	Info.ExpireDuration = 5.0f;
	FSlateNotificationManager::Get().AddNotification(Info);

	UE_LOG(LogClothingAsset, Warning, TEXT("%s"), *Error.ToString());
}

bool UClothingAssetCommon::BindToSkeletalMesh(
	USkeletalMesh* InSkelMesh, 
	const int32 InMeshLodIndex, 
	const int32 InSectionIndex, 
	const int32 InAssetLodIndex)
{
	// Make sure the legacy LOD are upgraded (BindToSkeletalMesh could be called before the Cloth Asset's PostLoad is completed)
	for (UClothLODDataCommon_Legacy* LodDeprecated : ClothLodData_DEPRECATED)
	{
		if (LodDeprecated)
		{
			LodDeprecated->ConditionalPostLoad();

			const int32 Idx = AddNewLod();
			LodDeprecated->MigrateTo(LodData[Idx]);
		}
	}
	ClothLodData_DEPRECATED.Empty();

	// If we've been added to the wrong mesh
	if(InSkelMesh != GetOuter())
	{
		FText Error = FText::Format(
			LOCTEXT("Error_WrongMesh", "Failed to bind clothing asset {0} as the provided mesh is not the owner of this asset."), 
			FText::FromString(GetName()));
		Warn(Error);
		return false;
	}

	// If we don't have clothing data
	if(!LodData.IsValidIndex(InAssetLodIndex))
	{
		FText Error = FText::Format(
			LOCTEXT("Error_NoClothingLod", "Failed to bind clothing asset {0} LOD{1} as LOD{2} does not exist."), 
			FText::FromString(GetName()), 
			InAssetLodIndex,
			InAssetLodIndex);
		Warn(Error);
		return false;
	}

	// If we don't have a mesh
	if(!InSkelMesh)
	{
		FText Error = FText::Format(
			LOCTEXT("Error_NoMesh", "Failed to bind clothing asset {0} as provided skel mesh does not exist."), 
			FText::FromString(GetName()));
		Warn(Error);
		return false;
	}

	// If the mesh LOD index is invalid
	if(!InSkelMesh->GetImportedModel()->LODModels.IsValidIndex(InMeshLodIndex))
	{
		FText Error = FText::Format(
			LOCTEXT("Error_InvalidMeshLOD", "Failed to bind clothing asset {0} as mesh LOD{1} does not exist."), 
			FText::FromString(GetName()), 
			InMeshLodIndex);
		Warn(Error);
		return false;
	}

	const int32 NumMapEntries = LodMap.Num();
	for(int MapIndex = 0; MapIndex < NumMapEntries; ++MapIndex)
	{
		const int32& MappedLod = LodMap[MapIndex];
		if(MappedLod == InAssetLodIndex)
		{
			FText Error = FText::Format(
				LOCTEXT("Error_LodMapped", "Failed to bind clothing asset {0} LOD{1} as LOD{2} is already mapped to mesh LOD{3}."), 
				FText::FromString(GetName()), 
				InAssetLodIndex, 
				InAssetLodIndex, 
				MapIndex);
			Warn(Error);
			return false;
		}
	}

	if(LodMap.IsValidIndex(InMeshLodIndex) && LodMap[InMeshLodIndex] != INDEX_NONE)
	{
		// Already mapped
		return false;
	}

	BuildSelfCollisionData();
	CalculateReferenceBoneIndex();

	// Grab the clothing and skel lod data
	FClothLODDataCommon& ClothLodData = LodData[InAssetLodIndex];
	FSkeletalMeshLODModel& SkelLod = InSkelMesh->GetImportedModel()->LODModels[InMeshLodIndex];

	FSkelMeshSection& OriginalSection = SkelLod.Sections[InSectionIndex];

	// Data for mesh to mesh binding
	TArray<FMeshToMeshVertData> MeshToMeshData;
	TArray<FVector> RenderPositions;
	TArray<FVector> RenderNormals;
	TArray<FVector> RenderTangents;

	RenderPositions.Reserve(OriginalSection.SoftVertices.Num());
	RenderNormals.Reserve(OriginalSection.SoftVertices.Num());
	RenderTangents.Reserve(OriginalSection.SoftVertices.Num());

	// Original data to weight to the clothing simulation mesh
	for(FSoftSkinVertex& UnrealVert : OriginalSection.SoftVertices)
	{
		RenderPositions.Add(UnrealVert.Position);
		RenderNormals.Add(UnrealVert.TangentZ);
		RenderTangents.Add(UnrealVert.TangentX);
	}

	TArrayView<uint32> IndexView(SkelLod.IndexBuffer);
	IndexView.Slice(OriginalSection.BaseIndex, OriginalSection.NumTriangles * 3);

	ClothingMeshUtils::ClothMeshDesc TargetMesh(RenderPositions, RenderNormals, IndexView);
	ClothingMeshUtils::ClothMeshDesc SourceMesh(
		ClothLodData.PhysicalMeshData.Vertices, 
		ClothLodData.PhysicalMeshData.Normals, 
		ClothLodData.PhysicalMeshData.Indices);

	ClothingMeshUtils::GenerateMeshToMeshSkinningData(MeshToMeshData, TargetMesh, &RenderTangents, SourceMesh, 
		ClothLodData.bUseMultipleInfluences, ClothLodData.SkinningKernelRadius);

	if(MeshToMeshData.Num() == 0)
	{
		// Failed to generate skinning data, the function above will have notified
		// with the cause of the failure, so just exit
		return false;
	}

	// Calculate fixed verts
	const FPointWeightMap* const MaxDistances = ClothLodData.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
	if (MaxDistances && MaxDistances->Num())
	{
		for(FMeshToMeshVertData& VertData : MeshToMeshData)
		{
			if (MaxDistances->AreAllBelowThreshold(VertData.SourceMeshVertIndices[0], VertData.SourceMeshVertIndices[1], VertData.SourceMeshVertIndices[2]))
			{
				VertData.SourceMeshVertIndices[3] = 0xFFFF;
			}
		}
	}

	// We have to copy the bone map to verify we don't exceed the maximum while adding the clothing bones
	TArray<FBoneIndexType> TempBoneMap = OriginalSection.BoneMap;
	for(FName& BoneName : UsedBoneNames)
	{
		const int32 BoneIndex = InSkelMesh->GetRefSkeleton().FindBoneIndex(BoneName);
		if(BoneIndex != INDEX_NONE)
		{
			TempBoneMap.AddUnique(BoneIndex);
		}
	}
	
	// Verify number of bones against current capabilities
	if(TempBoneMap.Num() > FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones())
	{
		// Failed to apply as we've exceeded the number of bones we can skin
		FText Error = FText::Format(
			LOCTEXT("Error_TooManyBones", "Failed to bind clothing asset {0} LOD{1} as this causes the section to require {2} bones. The maximum per section is currently {3}."), 
			FText::FromString(GetName()), 
			InAssetLodIndex, 
			TempBoneMap.Num(), 
			FGPUBaseSkinVertexFactory::GetMaxGPUSkinBones());
		Warn(Error);
		return false;
	}

	// After verifying copy the new bone map to the section
	OriginalSection.BoneMap = TempBoneMap;

	//Register the scope post edit change
	FScopedSkeletalMeshPostEditChange SkeletalMeshPostEditChange(InSkelMesh);

	// calculate LOD verts before adding our new section
	uint32 NumLodVertices = 0;
	for(const FSkelMeshSection& CurSection : SkelLod.Sections)
	{
		NumLodVertices += CurSection.GetNumVertices();
	}

	// Set the asset index, used during rendering to pick the correct sim mesh buffer
	int32 AssetIndex = INDEX_NONE;
	check(InSkelMesh->GetMeshClothingAssets().Find(this, AssetIndex));
	OriginalSection.CorrespondClothAssetIndex = AssetIndex;

	// sim properties
	OriginalSection.ClothMappingData = MeshToMeshData;
	OriginalSection.ClothingData.AssetGuid = AssetGuid;
	OriginalSection.ClothingData.AssetLodIndex = InAssetLodIndex;

	bool bRequireBoneChange = false;
	for(FBoneIndexType& BoneIndex : OriginalSection.BoneMap)
	{
		if(!SkelLod.RequiredBones.Contains(BoneIndex))
		{
			bRequireBoneChange = true;
			if(InSkelMesh->GetRefSkeleton().IsValidIndex(BoneIndex))
			{
				SkelLod.RequiredBones.Add(BoneIndex);
				SkelLod.ActiveBoneIndices.AddUnique(BoneIndex);
			}
		}
	}
	if(bRequireBoneChange)
	{
		SkelLod.RequiredBones.Sort();
		InSkelMesh->GetRefSkeleton().EnsureParentsExistAndSort(SkelLod.ActiveBoneIndices);
	}

	if(CustomData)
	{
		CustomData->BindToSkeletalMesh(InSkelMesh, InMeshLodIndex, InSectionIndex, InAssetLodIndex);
	}

	// Make sure the LOD map is always big enough for the asset to use.
	// This shouldn't grow to an unwieldy size but maybe consider compacting later.
	while(LodMap.Num() - 1 < InMeshLodIndex)
	{
		LodMap.Add(INDEX_NONE);
	}

	LodMap[InMeshLodIndex] = InAssetLodIndex;

	return true;

	// FScopedSkeletalMeshPostEditChange goes out of scope, causing postedit change and components to be re-registered
}

void UClothingAssetCommon::UnbindFromSkeletalMesh(USkeletalMesh* InSkelMesh)
{
	if(FSkeletalMeshModel* Mesh = InSkelMesh->GetImportedModel())
	{
		const int32 NumLods = Mesh->LODModels.Num();
		for(int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
		{
			UnbindFromSkeletalMesh(InSkelMesh, LodIndex);
		}
	}
}

void UClothingAssetCommon::UnbindFromSkeletalMesh(
	USkeletalMesh* InSkelMesh, 
	const int32 InMeshLodIndex)
{
	bool bChangedMesh = false;

	// Find the chunk(s) we created
	if(FSkeletalMeshModel* Mesh = InSkelMesh->GetImportedModel())
	{
		if(!Mesh->LODModels.IsValidIndex(InMeshLodIndex))
		{
			FText Error = FText::Format(
				LOCTEXT("Error_UnbindNoMeshLod", "Failed to remove clothing asset {0} from mesh LOD{1} as that LOD doesn't exist."), 
				FText::FromString(GetName()), 
				InMeshLodIndex);
			Warn(Error);

			return;
		}

		FSkeletalMeshLODModel& LodModel = Mesh->LODModels[InMeshLodIndex];

		for(int32 SectionIdx = LodModel.Sections.Num() - 1; SectionIdx >= 0; --SectionIdx)
		{
			FSkelMeshSection& Section = LodModel.Sections[SectionIdx];
			if(Section.HasClothingData() && Section.ClothingData.AssetGuid == AssetGuid)
			{
				InSkelMesh->PreEditChange(nullptr);
				ClothingAssetUtils::ClearSectionClothingData(Section);
				if (FSkelMeshSourceSectionUserData* UserSectionData = LodModel.UserSectionsData.Find(Section.OriginalDataSectionIndex))
				{
					UserSectionData->CorrespondClothAssetIndex = INDEX_NONE;
					UserSectionData->ClothingData.AssetLodIndex = INDEX_NONE;
					UserSectionData->ClothingData.AssetGuid = FGuid();
				}
				bChangedMesh = true;
			}
		}

		// Clear the LOD map entry for this asset LOD, after a unbind we must be able to bind any asset
		if (LodMap.IsValidIndex(InMeshLodIndex))
		{
			LodMap[InMeshLodIndex] = INDEX_NONE;
			bChangedMesh = true;
		}
	}

	// If the mesh changed we need to re-register any components that use it to reflect the changes
	if(bChangedMesh)
	{
		//Register the scope post edit change
		FScopedSkeletalMeshPostEditChange SkeletalMeshPostEditChange(InSkelMesh);
	}
}

void UClothingAssetCommon::ReregisterComponentsUsingClothing()
{
	if(USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(GetOuter()))
	{
		for(TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			if(USkeletalMeshComponent* Component = *It)
			{
				if(Component->SkeletalMesh == OwnerMesh)
				{
					FComponentReregisterContext Context(Component);
					// Context goes out of scope, causing Component to be re-registered
				}
			}
		}
	}
}

void UClothingAssetCommon::ForEachInteractorUsingClothing(TFunction<void(UClothingSimulationInteractor*)> Func)
{
	if(USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(GetOuter()))
	{
		for(TObjectIterator<USkeletalMeshComponent> It; It; ++It)
		{
			if(USkeletalMeshComponent* Component = *It)
			{
				if(Component->SkeletalMesh == OwnerMesh)
				{
					if(UClothingSimulationInteractor* CurInteractor = Component->GetClothingSimulationInteractor())
					{
						Func(CurInteractor);
					}
				}
			}
		}
	}
}

void UClothingAssetCommon::ApplyParameterMasks(bool bUpdateFixedVertData)
{
	for(FClothLODDataCommon& Lod : LodData)
	{
		Lod.PushWeightsToMesh();
	}
	InvalidateCachedData();

	// Recompute weights if needed
	USkeletalMesh* const SkeletalMesh = Cast<USkeletalMesh>(GetOuter());
	
	if (bUpdateFixedVertData && SkeletalMesh)
	{
		FSkeletalMeshModel* Resource = SkeletalMesh->GetImportedModel();
		FScopedSkeletalMeshPostEditChange ScopedSkeletalMeshPostEditChange(SkeletalMesh);

		SkeletalMesh->PreEditChange(nullptr);

		for (FSkeletalMeshLODModel& LodModel : Resource->LODModels)
		{
			for (FSkelMeshSection& Section : LodModel.Sections)
			{
				if (!Section.HasClothingData() || Cast<UClothingAssetCommon>(SkeletalMesh->GetClothingAsset(Section.ClothingData.AssetGuid)) != this)
				{
					continue;
				}
				const FClothLODDataCommon& LodDatum = LodData[Section.ClothingData.AssetLodIndex];
				const FPointWeightMap* const MaxDistances = LodDatum.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);

				if (MaxDistances && MaxDistances->Num())
				{
					for (FMeshToMeshVertData& VertData : Section.ClothMappingData)
					{
						VertData.SourceMeshVertIndices[3] = MaxDistances->AreAllBelowThreshold(
							VertData.SourceMeshVertIndices[0],
							VertData.SourceMeshVertIndices[1],
							VertData.SourceMeshVertIndices[2]) ? 0xFFFF : 0;
					}
				}
				else
				{
					for (FMeshToMeshVertData& VertData : Section.ClothMappingData)
					{
						VertData.SourceMeshVertIndices[3] = 0;
					}
				}
			}
		}
		// We must always dirty the DDC key unless previewing
		SkeletalMesh->InvalidateDeriveDataCacheGUID();
	}
}

void UClothingAssetCommon::BuildLodTransitionData()
{
	const int32 NumLods = GetNumLods();
	for(int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		const bool bHasPrevLod = LodIndex > 0;
		const bool bHasNextLod = LodIndex < NumLods - 1;

		FClothLODDataCommon& CurrentLod = LodData[LodIndex];
		const FClothPhysicalMeshData& CurrentPhysMesh = CurrentLod.PhysicalMeshData;

		FClothLODDataCommon* const PrevLod = bHasPrevLod ? &LodData[LodIndex - 1] : nullptr;
		FClothLODDataCommon* const NextLod = bHasNextLod ? &LodData[LodIndex + 1] : nullptr;

		const int32 CurrentLodNumVerts = CurrentPhysMesh.Vertices.Num();

		ClothingMeshUtils::ClothMeshDesc CurrentMeshDesc(CurrentPhysMesh.Vertices, CurrentPhysMesh.Normals, CurrentPhysMesh.Indices);
		static const bool bUseMultipleInfluences = false;  // Multiple influences must not be used for LOD transitions
		if(PrevLod)
		{
			FClothPhysicalMeshData& PrevPhysMesh = PrevLod->PhysicalMeshData;
			CurrentLod.TransitionUpSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc PrevMeshDesc(PrevPhysMesh.Vertices, PrevPhysMesh.Normals, PrevPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.TransitionUpSkinData, CurrentMeshDesc, nullptr, 
				PrevMeshDesc, bUseMultipleInfluences, CurrentLod.SkinningKernelRadius);
		}
		if(NextLod)
		{
			FClothPhysicalMeshData& NextPhysMesh = NextLod->PhysicalMeshData;
			CurrentLod.TransitionDownSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc NextMeshDesc(NextPhysMesh.Vertices, NextPhysMesh.Normals, NextPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.TransitionDownSkinData, CurrentMeshDesc, 
				nullptr, NextMeshDesc, bUseMultipleInfluences, CurrentLod.SkinningKernelRadius);
		}
	}
}

#endif // WITH_EDITOR

void UClothingAssetCommon::RefreshBoneMapping(USkeletalMesh* InSkelMesh)
{
	// No mesh, can't remap
	if(!InSkelMesh)
	{
		return;
	}

	if(UsedBoneNames.Num() != UsedBoneIndices.Num())
	{
		UsedBoneIndices.Reset();
		UsedBoneIndices.AddDefaulted(UsedBoneNames.Num());
	}

	// Repopulate the used indices.
	for(int32 BoneNameIndex = 0; BoneNameIndex < UsedBoneNames.Num(); ++BoneNameIndex)
	{
		UsedBoneIndices[BoneNameIndex] = 
			InSkelMesh->GetRefSkeleton().FindBoneIndex(
				UsedBoneNames[BoneNameIndex]);
	}
}

void UClothingAssetCommon::CalculateReferenceBoneIndex()
{
	// Starts at root
	ReferenceBoneIndex = 0;

#if WITH_APEX_CLOTHING
	const TSubclassOf<class UClothingSimulationFactory> ClothingSimulationFactory = UClothingSimulationFactory::GetDefaultClothingSimulationFactoryClass();
	if (ClothingSimulationFactory->GetName() == TEXT("ClothingSimulationFactoryNv"))
	{
		return;
	}
#endif

	// Find the root bone for this clothing asset (common bone for all used bones)
	typedef TArray<int32> BoneIndexArray;

	// List of valid paths to the root bone from each weighted bone
	TArray<BoneIndexArray> PathsToRoot;
	
	USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(GetOuter());

	if(OwnerMesh)
	{
		// First build a list per used bone for it's path to root
		TArray<int32> WeightedBones;  // List of actually weighted (not just used) bones

		for(FClothLODDataCommon& CurLod : LodData)
		{
			const FClothPhysicalMeshData& MeshData = CurLod.PhysicalMeshData;
			for(const FClothVertBoneData& VertBoneData : MeshData.BoneData)
			{
				for(int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
				{
					if(VertBoneData.BoneWeights[InfluenceIndex] > SMALL_NUMBER)
					{
						const int32 UnmappedBoneIndex = VertBoneData.BoneIndices[InfluenceIndex];
						check(UsedBoneIndices.IsValidIndex(UnmappedBoneIndex));
						WeightedBones.AddUnique(UsedBoneIndices[UnmappedBoneIndex]);
					}
					else
					{
						// Hit the last weight (they're sorted)
						break;
					}
				}
			}
		}

		const int32 NumWeightedBones = WeightedBones.Num();
		PathsToRoot.Reserve(NumWeightedBones);
		
		// Compute paths to the root bone
		const FReferenceSkeleton& RefSkel = OwnerMesh->GetRefSkeleton();
		for(int32 WeightedBoneIndex = 0; WeightedBoneIndex < NumWeightedBones; ++WeightedBoneIndex)
		{
			PathsToRoot.AddDefaulted();
			BoneIndexArray& Path = PathsToRoot.Last();
			
			int32 CurrentBone = WeightedBones[WeightedBoneIndex];
			Path.Add(CurrentBone);
			
			while(CurrentBone != 0 && CurrentBone != INDEX_NONE)
			{
				CurrentBone = RefSkel.GetParentIndex(CurrentBone);
				Path.Add(CurrentBone);
			}
		}

		// Paths are from leaf->root, we want the other way
		for(BoneIndexArray& Path : PathsToRoot)
		{
			Algo::Reverse(Path);
		}

		// Verify the last common bone in all paths as the root of the sim space
		const int32 NumPaths = PathsToRoot.Num();
		if(NumPaths > 0)
		{
			BoneIndexArray& FirstPath = PathsToRoot[0];
		
			const int32 FirstPathSize = FirstPath.Num();
			for(int32 PathEntryIndex = 0; PathEntryIndex < FirstPathSize; ++PathEntryIndex)
			{
				const int32 CurrentQueryIndex = FirstPath[PathEntryIndex];
				bool bValidRoot = true;

				for(int32 PathIndex = 1; PathIndex < NumPaths; ++PathIndex)
				{
					if(!PathsToRoot[PathIndex].Contains(CurrentQueryIndex))
					{
						bValidRoot = false;
						break;
					}
				}

				if(bValidRoot)
				{
					ReferenceBoneIndex = CurrentQueryIndex;
				}
				else
				{
					// Once we fail to find a valid root we're done.
					break;
				}
			}
		}
		else
		{
			// Just use root
			ReferenceBoneIndex = 0;
		}
	}
}

bool UClothingAssetCommon::IsValidLod(int32 InLodIndex) const
{
	return LodData.IsValidIndex(InLodIndex);
}

int32 UClothingAssetCommon::GetNumLods() const
{
	return LodData.Num();
}

void UClothingAssetCommon::BuildSelfCollisionData()
{
	if (ClothConfigs.Num())
	{
		for(FClothLODDataCommon& Lod : LodData)
		{
			Lod.PhysicalMeshData.BuildSelfCollisionData(ClothConfigs);
		}
	}
}

void UClothingAssetCommon::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	// Migrate the deprecated UObject based lod class to the non-UObject lod structure to prevent PostLoad dependency issues
	// TODO: Remove all UObject PostLoad dependencies.
	//       Even with these ConditionalPostLoad calls, the UObject PostLoads' order of execution cannot be guaranteed.
	for (UClothLODDataCommon_Legacy* LodDeprecated : ClothLodData_DEPRECATED)
	{
		if (LodDeprecated)
		{
			LodDeprecated->ConditionalPostLoad();

			const int32 Idx = AddNewLod();
			LodDeprecated->MigrateTo(LodData[Idx]);
		}
	}
	ClothLodData_DEPRECATED.Empty();
#endif

	const int32 AnimPhysCustomVersion = GetLinkerCustomVersion(FAnimPhysObjectVersion::GUID);
	if (AnimPhysCustomVersion < FAnimPhysObjectVersion::AddedClothingMaskWorkflow)
	{
#if WITH_EDITORONLY_DATA
		// Convert current parameters to masks
		for (FClothLODDataCommon& Lod : LodData)
		{
			const FClothPhysicalMeshData& PhysMesh = Lod.PhysicalMeshData;

			// Didn't do anything previously - clear out in case there's something in there
			// so we can use it correctly now.
			Lod.PointWeightMaps.Reset(3);

			// Max distances
			const FPointWeightMap* const MaxDistances = PhysMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
			if (MaxDistances)
			{
				Lod.PointWeightMaps.AddDefaulted();
				FPointWeightMap& MaxDistanceMask = Lod.PointWeightMaps.Last();
				MaxDistanceMask.Initialize(*MaxDistances, EWeightMapTargetCommon::MaxDistance);
			}

			// Following params are only added if necessary, if we don't have any backstop
			// radii then there's no backstops.
			const FPointWeightMap* const BackstopRadiuses = PhysMesh.FindWeightMap(EWeightMapTargetCommon::BackstopRadius);
			if (BackstopRadiuses && !BackstopRadiuses->IsZeroed())
			{
				// Backstop radii
				Lod.PointWeightMaps.AddDefaulted();
				FPointWeightMap& BackstopRadiusMask = Lod.PointWeightMaps.Last();
				BackstopRadiusMask.Initialize(*BackstopRadiuses, EWeightMapTargetCommon::BackstopRadius);

				// Backstop distances
				Lod.PointWeightMaps.AddDefaulted();
				FPointWeightMap& BackstopDistanceMask = Lod.PointWeightMaps.Last();
				const FPointWeightMap& BackstopDistances = PhysMesh.GetWeightMap(EWeightMapTargetCommon::BackstopDistance);
				BackstopDistanceMask.Initialize(BackstopDistances, EWeightMapTargetCommon::BackstopDistance);
			}
		}
#endif

		// Make sure we're transactional
		SetFlags(RF_Transactional);
	}

	const int32 ClothingCustomVersion = GetLinkerCustomVersion(FClothingAssetCustomVersion::GUID);
#if WITH_EDITORONLY_DATA
	// Fix content imported before we kept vertex colors
	if (ClothingCustomVersion < FClothingAssetCustomVersion::AddVertexColorsToPhysicalMesh)
	{
		for (FClothLODDataCommon& Lod : LodData)
		{
			const int32 NumVerts = Lod.PhysicalMeshData.Vertices.Num(); // number of verts

			Lod.PhysicalMeshData.VertexColors.Reset();
			Lod.PhysicalMeshData.VertexColors.AddUninitialized(NumVerts);
			for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
			{
				Lod.PhysicalMeshData.VertexColors[VertIdx] = FColor::White;
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	if (AnimPhysCustomVersion < FAnimPhysObjectVersion::CacheClothMeshInfluences)
	{
		// Rebuild data cache
		InvalidateCachedData();
	}
#endif

	// Add any missing configs for the available cloth factories, and try to migrate them from any existing one
	// TODO: Remove all UObject PostLoad dependencies.
	//       Even with these ConditionalPostLoad calls, the UObject PostLoads' order of execution cannot be guaranteed.
	for (TPair<FName, UClothConfigBase*>& ClothConfig : ClothConfigs)
	{
		if (ClothConfig.Value)
		{
			ClothConfig.Value->ConditionalPostLoad();  // PostLoad configs before adding new ones
		}
	}
#if WITH_EDITORONLY_DATA
	if (ClothSimConfig_DEPRECATED)
	{
		ClothSimConfig_DEPRECATED->ConditionalPostLoad();  // PostLoad old configs before replacing them
		ClothSimConfig_DEPRECATED->Rename(nullptr, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);  // Rename the config so that the name doesn't collide with the new config map name
	}
	if (ChaosClothSimConfig_DEPRECATED)
	{
		ChaosClothSimConfig_DEPRECATED->ConditionalPostLoad();  // PostLoad old configs before replacing them
		ChaosClothSimConfig_DEPRECATED->Rename(nullptr, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);  // Rename the config so that the name doesn't collide with the new config map name
	}
	if (ClothSharedSimConfig_DEPRECATED)
	{
		ClothSharedSimConfig_DEPRECATED->ConditionalPostLoad();  // PostLoad old configs before replacing them
		ClothSharedSimConfig_DEPRECATED->Rename(nullptr, nullptr, REN_DoNotDirty | REN_DontCreateRedirectors | REN_ForceNoResetLoaders | REN_NonTransactional);  // Rename the config so that the name doesn't collide with the new config map name
	}
#endif
	AddClothConfigs();

	// Migrate configs
	bool bMigrateSharedConfigToConfig = true;  // Shared config to config migration can be disabled to avoid overriding the newly migrated values

#if WITH_EDITORONLY_DATA
	if (ClothingCustomVersion < FClothingAssetCustomVersion::MovePropertiesToCommonBaseClasses)
	{
		// Remap legacy struct FClothConfig to new config objects
		for (TPair<FName, UClothConfigBase*>& ClothConfig : ClothConfigs)
		{
			if (UClothConfigCommon* const ClothConfigCommon = Cast<UClothConfigCommon>(ClothConfig.Value))
			{
				ClothConfigCommon->ConditionalPostLoad();
				ClothConfigCommon->MigrateFrom(ClothConfig_DEPRECATED);
			}
		}
		bMigrateSharedConfigToConfig = false;
	}
	else
	{
		// Migrate simulation dependent config parameters to the new config map
		if (ClothSimConfig_DEPRECATED)
		{
			// Try a remap to the new config objects through the legacy structure
			if (const UClothConfigCommon* const ClothSimConfigCommon = Cast<UClothConfigCommon>(ClothSimConfig_DEPRECATED))
			{
				FClothConfig_Legacy ClothConfigLegacy;
				if (ClothSimConfigCommon->MigrateTo(ClothConfigLegacy))
				{
					for (TPair<FName, UClothConfigBase*>& ClothConfig : ClothConfigs)
					{
						if (UClothConfigCommon* const ClothConfigCommon = Cast<UClothConfigCommon>(ClothConfig.Value))
						{
							ClothConfigCommon->ConditionalPostLoad();
							ClothConfigCommon->MigrateFrom(ClothConfigLegacy);
						}
					}
				}
			}
			// And keep the old config too
			SetClothConfig(ClothSimConfig_DEPRECATED);
			ClothSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
		if (ChaosClothSimConfig_DEPRECATED)
		{
			SetClothConfig(ChaosClothSimConfig_DEPRECATED);
			ChaosClothSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
		if (ClothSharedSimConfig_DEPRECATED)
		{
			SetClothConfig(ClothSharedSimConfig_DEPRECATED);
			ClothSharedSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
	}
#endif

	// Propagate shared configs between cloth assets
	PropagateSharedConfigs(bMigrateSharedConfigToConfig);

	// After fixing the content, we are ready to call functions that rely on it
	BuildSelfCollisionData();
#if WITH_EDITORONLY_DATA
	CalculateReferenceBoneIndex();

	const int32 PhysicsObjectVersion = GetLinkerCustomVersion(FPhysicsObjectVersion::GUID);
	if (PhysicsObjectVersion < FPhysicsObjectVersion::ChaosClothFixLODTransitionMaps)
	{
		BuildLodTransitionData();
	}
#endif
}

void UClothingAssetCommon::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	Ar.UsingCustomVersion(FClothingAssetCustomVersion::GUID);
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
}

void UClothingAssetCommon::AddClothConfigs()
{
	const TArray<IClothingSimulationFactoryClassProvider*> ClassProviders =
		IModularFeatures::Get().GetModularFeatureImplementations<IClothingSimulationFactoryClassProvider>(IClothingSimulationFactoryClassProvider::FeatureName);

	for (IClothingSimulationFactoryClassProvider* Provider : ClassProviders)
	{
		check(Provider);
		if (UClass* const ClothingSimulationFactoryClass = *TSubclassOf<class UClothingSimulationFactory>(Provider->GetClothingSimulationFactoryClass()))
		{
			const UClothingSimulationFactory* const ClothingSimulationFactory = ClothingSimulationFactoryClass->GetDefaultObject<UClothingSimulationFactory>();
			for (TSubclassOf<UClothConfigBase> ClothConfigClass : ClothingSimulationFactory->GetClothConfigClasses())
			{
				const FName ClothConfigName = ClothConfigClass->GetFName();
				if (!ClothConfigs.Find(ClothConfigName))
				{
					// Create new config object
					check(!StaticFindObject(ClothConfigClass, this, *ClothConfigClass->GetName(), true));
					UClothConfigBase* const ClothConfig = NewObject<UClothConfigBase>(this, ClothConfigClass, ClothConfigClass->GetFName(), RF_Transactional);

					// Use the legacy config struct to try find a common config as an acceptable migration source
					// This code could be removed once the legacy code is removed, although this will then prevent
					// migration from compatible config sources
					if (UClothConfigCommon* const ClothConfigCommon = Cast<UClothConfigCommon>(ClothConfig))
					{
						for (TPair<FName, UClothConfigBase*> ClothConfigPair : ClothConfigs)
						{
							if (const UClothConfigCommon* SourceConfig = Cast<UClothConfigCommon>(ClothConfigPair.Value))
							{
								FClothConfig_Legacy LegacyConfig;
								if (SourceConfig->MigrateTo(LegacyConfig))
								{
									ClothConfigCommon->MigrateFrom(LegacyConfig);
									break;
								}
							}
						}
					}

					// Add the new config
					check(ClothConfig);
					ClothConfigs.Add(ClothConfigName, ClothConfig);
				}
			}
		}
	}
}

void UClothingAssetCommon::PropagateSharedConfigs(bool bMigrateSharedConfigToConfig)
{
	// Update this asset's shared config when the asset belongs to a skeletal mesh
	if (USkeletalMesh* const SkeletalMesh = Cast<USkeletalMesh>(GetOuter()))
	{
		const TArray<UClothingAssetBase*>& ClothingAssets = SkeletalMesh->GetMeshClothingAssets();
 
		// Collect all shared configs found in the other assets
		TMap<FName, UClothConfigBase*> ClothSharedConfigs;

		for (const UClothingAssetBase* ClothingAssetBase : ClothingAssets)
		{
			if (ClothingAssetBase == static_cast<UClothingAssetBase* >(this))
			{
				continue;
			}

			// Only common assets have shared configs
			if (const UClothingAssetCommon* const ClothingAsset = Cast<UClothingAssetCommon>(ClothingAssetBase))
			{
				// Reserve space in the map, use the total number of configs in case they're unlikely all shared configs
				const int32 Max = ClothSharedConfigs.Num() + ClothingAsset->ClothConfigs.Num();
				ClothSharedConfigs.Reserve(Max);

				// Iterate through all configs, and find the shared ones
				for (const TPair<FName, UClothConfigBase*>& ClothSharedConfigItem : ClothingAsset->ClothConfigs)
				{
					if (Cast<UClothSharedConfigCommon>(ClothSharedConfigItem.Value) &&  // Only needs shared configs
						!ClothSharedConfigs.Find(ClothSharedConfigItem.Key))            // Only needs a single shared config per type
					{
						ClothSharedConfigs.Add(ClothSharedConfigItem);
					}
				}
			}
		}

		// Propagate the found shared configs to this asset
		for (const TPair<FName, UClothConfigBase*>& ClothSharedConfigItem : ClothSharedConfigs)
		{
			// Set share config
			if (UClothConfigBase** const ClothConfigBase = ClothConfigs.Find(ClothSharedConfigItem.Key))
			{
				// Reset this shared config
				*ClothConfigBase = ClothSharedConfigItem.Value;
			}
			else
			{
				// Add new map entry
				ClothConfigs.Add(ClothSharedConfigItem);
			}
		}

		// Migrate the common shared configs' deprecated parameters to all per cloth configs
		if (bMigrateSharedConfigToConfig)
		{
			// Iterate through all this asset's shared configs
			for (const TPair<FName, UClothConfigBase*>& ClothSharedConfigItem : ClothConfigs)
			{
				if (const UClothSharedConfigCommon* const ClothSharedConfig = Cast<UClothSharedConfigCommon>(ClothSharedConfigItem.Value))
				{
					// Iterate through all this asset's configs, and migrate from the shared ones
					for (const TPair<FName, UClothConfigBase*>& ClothConfigItem : ClothConfigs)
					{
						if (Cast<UClothSharedConfigCommon>(ClothConfigItem.Value))
						{
							continue;  // Don't migrate shared configs to another shared configs (or itself)
						}
						if (UClothConfigCommon* const ClothConfig = Cast<UClothConfigCommon>(ClothConfigItem.Value))
						{
							ClothConfig->MigrateFrom(ClothSharedConfig);
						}
					}
				}
			}
		}
	}
}

void UClothingAssetCommon::PostUpdateAllAssets()
{
	// Add any missing configs for the available cloth factories, and try to migrate them from any existing one
	AddClothConfigs();

	// Propagate shared configs
	PropagateSharedConfigs();
}

#if WITH_EDITOR

void UClothingAssetCommon::InvalidateCachedData()
{
#if WITH_CHAOS_CLOTHING
	ForEachInteractorUsingClothing([](UClothingSimulationInteractor* InInteractor)
	{
		if (InInteractor)
		{
			InInteractor->ClothConfigUpdated();
		}
	});
#endif  // #if WITH_CHAOS_CLOTHING

	// TODO: This mass calculation isn't used by Chaos, check what can be stripped out
	for(FClothLODDataCommon& CurrentLodData : LodData)
	{
		// Recalculate inverse masses for the physical mesh particles
		FClothPhysicalMeshData& PhysMesh = CurrentLodData.PhysicalMeshData;
		check(PhysMesh.Indices.Num() % 3 == 0);

		TArray<float>& InvMasses = PhysMesh.InverseMasses;

		const int32 NumVerts = PhysMesh.Vertices.Num();
		InvMasses.Empty(NumVerts);
		InvMasses.AddZeroed(NumVerts);

		for(int32 TriBaseIndex = 0; TriBaseIndex < PhysMesh.Indices.Num(); TriBaseIndex += 3)
		{
			const int32 Index0 = PhysMesh.Indices[TriBaseIndex];
			const int32 Index1 = PhysMesh.Indices[TriBaseIndex + 1];
			const int32 Index2 = PhysMesh.Indices[TriBaseIndex + 2];

			const FVector AB = PhysMesh.Vertices[Index1] - PhysMesh.Vertices[Index0];
			const FVector AC = PhysMesh.Vertices[Index2] - PhysMesh.Vertices[Index0];
			const float TriArea = FVector::CrossProduct(AB, AC).Size();

			InvMasses[Index0] += TriArea;
			InvMasses[Index1] += TriArea;
			InvMasses[Index2] += TriArea;
		}

		PhysMesh.NumFixedVerts = 0;

		const FPointWeightMap* const MaxDistances = PhysMesh.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
		const TFunction<bool(int32)> IsKinematic = (!MaxDistances || !MaxDistances->Num()) ?
			TFunction<bool(int32)>([](int32)->bool { return false; }) :
			TFunction<bool(int32)>([&MaxDistances](int32 Index)->bool { return (*MaxDistances)[Index] < SMALL_NUMBER; });  // For consistency, the default Threshold should be 0.1, not SMALL_NUMBER. But for backward compatibility it needs to be SMALL_NUMBER for now.

		float MassSum = 0.0f;
		for (int32 CurrVertIndex = 0; CurrVertIndex < NumVerts; ++CurrVertIndex)
		{
			float& InvMass = InvMasses[CurrVertIndex];

			if (IsKinematic(CurrVertIndex))
			{
				InvMass = 0.0f;
				++PhysMesh.NumFixedVerts;
			}
			else
			{
				MassSum += InvMass;
			}
		}

		if (MassSum > 0.0f)
		{
			const float MassScale = (float)(NumVerts - PhysMesh.NumFixedVerts) / MassSum;
			for (float& InvMass : InvMasses)
			{
				if (InvMass != 0.0f)
				{
					InvMass *= MassScale;
					InvMass = 1.0f / InvMass;
				}
			}
		}

		// Calculate number of influences per vertex
		for(int32 VertIndex = 0; VertIndex < NumVerts; ++VertIndex)
		{
			FClothVertBoneData& BoneData = PhysMesh.BoneData[VertIndex];
			const uint16* BoneIndices = BoneData.BoneIndices;
			const float* BoneWeights = BoneData.BoneWeights;

			BoneData.NumInfluences = MAX_TOTAL_INFLUENCES;

			int32 NumInfluences = 0;
			for(int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
			{
				if(BoneWeights[InfluenceIndex] == 0.0f || BoneIndices[InfluenceIndex] == INDEX_NONE)
				{
					BoneData.NumInfluences = NumInfluences;
					break;
				}
				++NumInfluences;
			}
		}
	}
}

int32 UClothingAssetCommon::AddNewLod()
{
	return LodData.AddDefaulted();
}

void UClothingAssetCommon::PostEditChangeChainProperty(FPropertyChangedChainEvent& ChainEvent)
{
	Super::PostEditChangeChainProperty(ChainEvent);

	bool bReregisterComponents = false;

	if (ChainEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		if (ChainEvent.Property->GetFName() == FName("SelfCollisionRadius") ||
			ChainEvent.Property->GetFName() == FName("SelfCollisionCullScale"))
		{
			BuildSelfCollisionData();
			bReregisterComponents = true;
		}
		else if(ChainEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UClothingAssetCommon, PhysicsAsset))
		{
			bReregisterComponents = true;
		}
		else
		{
			// Other properties just require a config refresh
			ForEachInteractorUsingClothing([](UClothingSimulationInteractor* InInteractor)
			{
				if (InInteractor)
				{
					InInteractor->ClothConfigUpdated();
				}
			});
		}
	}

	if (bReregisterComponents)
	{
		ReregisterComponentsUsingClothing();
	}
}

#endif // WITH_EDITOR


#undef LOCTEXT_NAMESPACE
