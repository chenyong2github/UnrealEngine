// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothingAsset.h"

#include "Utils/ClothingMeshUtils.h"

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
	, ClothSimConfig(nullptr)
	, ChaosClothSimConfig(nullptr)
	, ReferenceBoneIndex(0)
	, CustomData(nullptr)
{}

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
	if(!ClothLodData.IsValidIndex(InAssetLodIndex))
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
	UClothLODDataBase* LodData = ClothLodData[InAssetLodIndex];
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
		LodData->PhysicalMeshData->Vertices, 
		LodData->PhysicalMeshData->Normals, 
		LodData->PhysicalMeshData->Indices);

	ClothingMeshUtils::GenerateMeshToMeshSkinningData(MeshToMeshData, TargetMesh, &RenderTangents, SourceMesh);

	if(MeshToMeshData.Num() == 0)
	{
		// Failed to generate skinning data, the function above will have notified
		// with the cause of the failure, so just exit
		return false;
	}

	// Calculate fixed verts
	for(FMeshToMeshVertData& VertData : MeshToMeshData)
	{
		if(LodData->PhysicalMeshData->IsFixed(
			VertData.SourceMeshVertIndices[0], 
			VertData.SourceMeshVertIndices[1], 
			VertData.SourceMeshVertIndices[2])) // Default threshold is 0.1, not 0.0.  Using 0.1 for consistency.
		{
			VertData.SourceMeshVertIndices[3] = 0xFFFF;
		}
	}

	// We have to copy the bone map to verify we don't exceed the maximum while adding the clothing bones
	TArray<FBoneIndexType> TempBoneMap = OriginalSection.BoneMap;
	for(FName& BoneName : UsedBoneNames)
	{
		const int32 BoneIndex = InSkelMesh->RefSkeleton.FindBoneIndex(BoneName);
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
	check(InSkelMesh->MeshClothingAssets.Find(this, AssetIndex));
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
			if(InSkelMesh->RefSkeleton.IsValidIndex(BoneIndex))
			{
				SkelLod.RequiredBones.Add(BoneIndex);
				SkelLod.ActiveBoneIndices.AddUnique(BoneIndex);
			}
		}
	}
	if(bRequireBoneChange)
	{
		SkelLod.RequiredBones.Sort();
		InSkelMesh->RefSkeleton.EnsureParentsExistAndSort(SkelLod.ActiveBoneIndices);		
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

void UClothingAssetCommon::ApplyParameterMasks()
{
	for(UClothLODDataBase* Lod : ClothLodData)
	{
		Lod->PushWeightsToMesh();
	}
	InvalidateCachedData();
}

void UClothingAssetCommon::BuildLodTransitionData()
{
	const int32 NumLods = GetNumLods();
	for(int32 LodIndex = 0; LodIndex < NumLods; ++LodIndex)
	{
		const bool bHasPrevLod = LodIndex > 0;
		const bool bHasNextLod = LodIndex < NumLods - 1;

		UClothLODDataBase* CurrentLod = ClothLodData[LodIndex];
		check(CurrentLod->PhysicalMeshData);
		UClothPhysicalMeshDataBase& CurrentPhysMesh = *CurrentLod->PhysicalMeshData;

		UClothLODDataBase* PrevLod = bHasPrevLod ? ClothLodData[LodIndex - 1] : nullptr;
		UClothLODDataBase* NextLod = bHasNextLod ? ClothLodData[LodIndex + 1] : nullptr;

		const int32 CurrentLodNumVerts = CurrentPhysMesh.Vertices.Num();

		ClothingMeshUtils::ClothMeshDesc CurrentMeshDesc(CurrentPhysMesh.Vertices, CurrentPhysMesh.Normals, CurrentPhysMesh.Indices);

		if(PrevLod)
		{
			check(PrevLod->PhysicalMeshData);
			UClothPhysicalMeshDataBase& PrevPhysMesh = *PrevLod->PhysicalMeshData;
			CurrentLod->TransitionUpSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc PrevMeshDesc(PrevPhysMesh.Vertices, PrevPhysMesh.Normals, PrevPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod->TransitionUpSkinData, CurrentMeshDesc, nullptr, PrevMeshDesc);
		}
		if(NextLod)
		{
			check(NextLod->PhysicalMeshData);
			UClothPhysicalMeshDataBase& NextPhysMesh = *NextLod->PhysicalMeshData;
			CurrentLod->TransitionDownSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc NextMeshDesc(NextPhysMesh.Vertices, NextPhysMesh.Normals, NextPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod->TransitionDownSkinData, CurrentMeshDesc, nullptr, NextMeshDesc);
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
			InSkelMesh->RefSkeleton.FindBoneIndex(
				UsedBoneNames[BoneNameIndex]);
	}
}

void UClothingAssetCommon::CalculateReferenceBoneIndex()
{
	// Starts at root
	ReferenceBoneIndex = 0;

	// Find the root bone for this clothing asset (common bone for all used bones)
	typedef TArray<int32> BoneIndexArray;

	// List of valid paths to the root bone from each weighted bone
	TArray<BoneIndexArray> PathsToRoot;
	
	USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(GetOuter());

	if(OwnerMesh)
	{
		FReferenceSkeleton& RefSkel = OwnerMesh->RefSkeleton;
		// First build a list per used bone for it's path to root
		const int32 NumUsedBones = UsedBoneIndices.Num();

		// List of actually weighted (not just used) bones
		TArray<int32> WeightedBones;

		for(UClothLODDataBase* CurLod : ClothLodData)
		{
			check(CurLod && CurLod->PhysicalMeshData);
			UClothPhysicalMeshDataBase* MeshData = CurLod->PhysicalMeshData;
			for(FClothVertBoneData& VertBoneData : MeshData->BoneData)
			{
				for(int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
				{
					if(VertBoneData.BoneWeights[InfluenceIndex] > SMALL_NUMBER)
					{
						WeightedBones.AddUnique(VertBoneData.BoneIndices[InfluenceIndex]);
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

bool UClothingAssetCommon::IsValidLod(int32 InLodIndex)
{
	return ClothLodData.IsValidIndex(InLodIndex);
}

int32 UClothingAssetCommon::GetNumLods()
{
	return ClothLodData.Num();
}

void UClothingAssetCommon::BuildSelfCollisionData()
{
	if(!ClothSimConfig || !ClothSimConfig->HasSelfCollision())
	{
		// No self collision, can't generate data
		return;
	}
	for(UClothLODDataBase* Lod : ClothLodData)
	{
		check(Lod && Lod->PhysicalMeshData);
		Lod->PhysicalMeshData->BuildSelfCollisionData(ClothSimConfig);
	}
}


#undef LOCTEXT_NAMESPACE
