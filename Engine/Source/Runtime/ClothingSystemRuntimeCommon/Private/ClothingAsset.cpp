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
#include "UObject/UE5PrivateFrostyStreamObjectVersion.h"

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
	, CustomData(nullptr)  // Deprecated
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

	CalculateReferenceBoneIndex();

	// Grab the clothing and skel lod data
	FClothLODDataCommon& ClothLodData = LodData[InAssetLodIndex];
	FSkeletalMeshLODModel& SkelLod = InSkelMesh->GetImportedModel()->LODModels[InMeshLodIndex];

	FSkelMeshSection& OriginalSection = SkelLod.Sections[InSectionIndex];

	// Data for mesh to mesh binding
	TArray<FMeshToMeshVertData> MeshToMeshData;
	TArray<FVector3f> RenderPositions;
	TArray<FVector3f> RenderNormals;
	TArray<FVector3f> RenderTangents;

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
	IndexView = IndexView.Slice(OriginalSection.BaseIndex, OriginalSection.NumTriangles * 3);

	TArray<uint32> RenderIndices;
	RenderIndices.Reserve(OriginalSection.NumTriangles * 3);
	for (uint32 OriginalIndex : IndexView)
	{
		const int32 TempIndex = (int32)OriginalIndex - (int32)OriginalSection.BaseVertexIndex;
		if (ensure(RenderPositions.IsValidIndex(TempIndex)))
		{
			RenderIndices.Add(TempIndex);
		}
	}

	const ClothingMeshUtils::ClothMeshDesc TargetMesh(RenderPositions, RenderNormals, RenderIndices);

	TArray<FVector3f> RecomputedVertexNormals;
	ClothLodData.PhysicalMeshData.ComputeFaceAveragedVertexNormals(RecomputedVertexNormals);

	const ClothingMeshUtils::ClothMeshDesc SourceMesh(
		ClothLodData.PhysicalMeshData.Vertices, 
		RecomputedVertexNormals,
		ClothLodData.PhysicalMeshData.Indices);

	TArray<float> MaxEdgeLength;
	ClothingMeshUtils::ComputeMaxEdgeLength(TargetMesh, MaxEdgeLength);

	ClothingMeshUtils::GenerateMeshToMeshSkinningData(MeshToMeshData, TargetMesh, &RenderTangents, SourceMesh, MaxEdgeLength,
		ClothLodData.bUseMultipleInfluences, ClothLodData.SkinningKernelRadius);

	if(MeshToMeshData.Num() == 0)
	{
		// Failed to generate skinning data, the function above will have notified
		// with the cause of the failure, so just exit
		return false;
	}

	// Calculate the vertex contribution alpha
	const FPointWeightMap* const MaxDistances = ClothLodData.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
	ClothingMeshUtils::ComputeVertexContributions(MeshToMeshData, MaxDistances, ClothLodData.bSmoothTransition);

	if (ClothLodData.bUseMultipleInfluences)
	{
		ClothingMeshUtils::FixZeroWeightVertices(MeshToMeshData, TargetMesh, &RenderTangents, SourceMesh, MaxEdgeLength);
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

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if(CustomData)  // Deprecated from 5.0 onward
	{
		CustomData->BindToSkeletalMesh(InSkelMesh, InMeshLodIndex, InSectionIndex, InAssetLodIndex);
	}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

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
	// Invalidate all cached data that depend on masks
	InvalidateFlaggedCachedData(EClothingCachedDataFlagsCommon::InverseMasses | EClothingCachedDataFlagsCommon::Tethers);

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

				ClothingMeshUtils::ComputeVertexContributions(Section.ClothMappingData, MaxDistances, LodDatum.bSmoothTransition);
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

		TArray<float> MaxEdgeLength;
		ClothingMeshUtils::ComputeMaxEdgeLength(CurrentMeshDesc, MaxEdgeLength);
		if(PrevLod)
		{
			FClothPhysicalMeshData& PrevPhysMesh = PrevLod->PhysicalMeshData;
			CurrentLod.TransitionUpSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc PrevMeshDesc(PrevPhysMesh.Vertices, PrevPhysMesh.Normals, PrevPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.TransitionUpSkinData, CurrentMeshDesc, nullptr, 
				PrevMeshDesc, MaxEdgeLength, bUseMultipleInfluences, CurrentLod.SkinningKernelRadius);
		}
		if(NextLod)
		{
			FClothPhysicalMeshData& NextPhysMesh = NextLod->PhysicalMeshData;
			CurrentLod.TransitionDownSkinData.Empty(CurrentLodNumVerts);
			ClothingMeshUtils::ClothMeshDesc NextMeshDesc(NextPhysMesh.Vertices, NextPhysMesh.Normals, NextPhysMesh.Indices);
			ClothingMeshUtils::GenerateMeshToMeshSkinningData(CurrentLod.TransitionDownSkinData, CurrentMeshDesc, 
				nullptr, NextMeshDesc, MaxEdgeLength, bUseMultipleInfluences, CurrentLod.SkinningKernelRadius);
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
	
	const USkeletalMesh* OwnerMesh = Cast<USkeletalMesh>(GetOuter());

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

	const int32 AnimPhysCustomVersion = GetLinkerCustomVersion(FAnimPhysObjectVersion::GUID);
	if (AnimPhysCustomVersion < FAnimPhysObjectVersion::AddedClothingMaskWorkflow)
	{
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

		// Make sure we're transactional
		SetFlags(RF_Transactional);
	}

	const int32 ClothingCustomVersion = GetLinkerCustomVersion(FClothingAssetCustomVersion::GUID);
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

	EClothingCachedDataFlagsCommon ClothingCachedDataFlags = EClothingCachedDataFlagsCommon::None;
	if (AnimPhysCustomVersion < FAnimPhysObjectVersion::CacheClothMeshInfluences)
	{
		ClothingCachedDataFlags |= EClothingCachedDataFlagsCommon::NumInfluences;
	}

	// Add any missing configs for the available cloth factories, and try to migrate them from any existing one
	// TODO: Remove all UObject PostLoad dependencies.
	//       Even with these ConditionalPostLoad calls, the UObject PostLoads' order of execution cannot be guaranteed.
	for (auto& ClothConfig : ClothConfigs)
	{
		if (ClothConfig.Value)
		{
			ClothConfig.Value->ConditionalPostLoad();  // PostLoad configs before adding new ones
		}
	}

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

	if (AddClothConfigs())
	{
		// With a new config added best to recache everything
		ClothingCachedDataFlags |= EClothingCachedDataFlagsCommon::All;
	}

	// Migrate configs
	bool bMigrateSharedConfigToConfig = true;  // Shared config to config migration can be disabled to avoid overriding the newly migrated values

	if (ClothingCustomVersion < FClothingAssetCustomVersion::MovePropertiesToCommonBaseClasses)
	{
		// Remap legacy struct FClothConfig to new config objects
		for (auto& ClothConfig : ClothConfigs)
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
					for (auto& ClothConfig : ClothConfigs)
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
			SetClothConfig(ToRawPtr(ClothSimConfig_DEPRECATED));
			ClothSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
		if (ChaosClothSimConfig_DEPRECATED)
		{
			SetClothConfig(ToRawPtr(ChaosClothSimConfig_DEPRECATED));
			ChaosClothSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
		if (ClothSharedSimConfig_DEPRECATED)
		{
			SetClothConfig(ToRawPtr(ClothSharedSimConfig_DEPRECATED));
			ClothSharedSimConfig_DEPRECATED = nullptr;
			bMigrateSharedConfigToConfig = false;
		}
	}

	// Propagate shared configs between cloth assets
	PropagateSharedConfigs(bMigrateSharedConfigToConfig);

	// Cache tethers and reference bone index should already have been calculated
	const int32 FrostyMainBranchObjectVersion = GetLinkerCustomVersion(FUE5PrivateFrostyStreamObjectVersion::GUID);
	if (FrostyMainBranchObjectVersion < FUE5PrivateFrostyStreamObjectVersion::ChaosClothAddTethersToCachedData)
	{
		ClothingCachedDataFlags |= EClothingCachedDataFlagsCommon::Tethers;

		// ReferenceBoneIndex is only required when rebinding the cloth.
		CalculateReferenceBoneIndex();
	}

	// After fixing the content, we are ready to call functions that rely on it
	if (ClothingCachedDataFlags != EClothingCachedDataFlagsCommon::None)
	{
		// Rebuild data cache
		InvalidateFlaggedCachedData(ClothingCachedDataFlags);
	}

	const int32 PhysicsObjectVersion = GetLinkerCustomVersion(FPhysicsObjectVersion::GUID);
	const int32 FortniteMainBranchObjectVersion = GetLinkerCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	if (PhysicsObjectVersion < FPhysicsObjectVersion::ChaosClothFixLODTransitionMaps ||
		FortniteMainBranchObjectVersion < FFortniteMainBranchObjectVersion::ChaosClothFixLODTransitionMaps)
	{
		BuildLodTransitionData();
	}
#endif  // #if WITH_EDITORONLY_DATA
}

void UClothingAssetCommon::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	Ar.UsingCustomVersion(FClothingAssetCustomVersion::GUID);
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	Ar.UsingCustomVersion(FUE5PrivateFrostyStreamObjectVersion::GUID);
}

bool UClothingAssetCommon::AddClothConfigs()
{
	bool bNewConfigAdded = false;

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
						for (auto ClothConfigPair : ClothConfigs)
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
					bNewConfigAdded = true;
				}
			}
		}
	}
	return bNewConfigAdded;
}

void UClothingAssetCommon::PropagateSharedConfigs(bool bMigrateSharedConfigToConfig)
{
	// Update this asset's shared config when the asset belongs to a skeletal mesh
	if (USkeletalMesh* const SkeletalMesh = Cast<USkeletalMesh>(GetOuter()))
	{
		const TArray<UClothingAssetBase*>& ClothingAssets = SkeletalMesh->GetMeshClothingAssets();
 
		// Collect all shared configs found in the other assets
		decltype(ClothConfigs) ClothSharedConfigs;

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
				for (const auto& ClothSharedConfigItem : ClothingAsset->ClothConfigs)
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
		for (const auto& ClothSharedConfigItem : ClothSharedConfigs)
		{
			// Set share config
			if (UE_TRANSITIONAL_OBJECT_PTR(UClothConfigBase)* const ClothConfigBase = ClothConfigs.Find(ClothSharedConfigItem.Key))
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
			for (const auto& ClothSharedConfigItem : ClothConfigs)
			{
				if (const UClothSharedConfigCommon* const ClothSharedConfig = Cast<UClothSharedConfigCommon>(ClothSharedConfigItem.Value))
				{
					// Iterate through all this asset's configs, and migrate from the shared ones
					for (const auto& ClothConfigItem : ClothConfigs)
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
	const bool bInvalidateCachedData = AddClothConfigs();

	// Propagate shared configs
	PropagateSharedConfigs();

#if WITH_EDITORONLY_DATA
	// Invalidate cached data if the configs have changed
	if (bInvalidateCachedData)
	{
		InvalidateAllCachedData();
	}
#endif
}


#if WITH_EDITORONLY_DATA
void UClothingAssetCommon::InvalidateFlaggedCachedData(EClothingCachedDataFlagsCommon Flags)
{
	const bool bNeedsInverseMasses = EnumHasAllFlags((EClothingCachedDataFlagsCommon)Flags, EClothingCachedDataFlagsCommon::InverseMasses) && AnyOfClothConfigs([](UClothConfigBase& Config) { return Config.NeedsInverseMasses(); });
	const bool bNeedsNumInfluences = EnumHasAllFlags((EClothingCachedDataFlagsCommon)Flags, EClothingCachedDataFlagsCommon::NumInfluences) && AnyOfClothConfigs([](UClothConfigBase& Config) { return Config.NeedsNumInfluences(); });
	const bool bNeedsSelfCollisionData = EnumHasAllFlags((EClothingCachedDataFlagsCommon)Flags, EClothingCachedDataFlagsCommon::SelfCollisionData) && AnyOfClothConfigs([](UClothConfigBase& Config) { return Config.NeedsSelfCollisionData(); });
	const bool bNeedsTethers = EnumHasAllFlags((EClothingCachedDataFlagsCommon)Flags, EClothingCachedDataFlagsCommon::Tethers) && AnyOfClothConfigs([](UClothConfigBase& Config) { return Config.NeedsTethers(); });

	float SelfCollisionRadius = 0.f;
	if (bNeedsSelfCollisionData)
	{
		// Note: Only PhysX based NvCloth needs to build the SelfCollisionIndices at the moment
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& ClothConfig : ClothConfigs)
		{
			SelfCollisionRadius = FMath::Max(SelfCollisionRadius, ClothConfig.Value->GetSelfCollisionRadius());
		}
	}

	bool bThethersUseEuclideanDistance = false;
	bool bThethersUseGeodesicDistance = false;
	if (bNeedsTethers)
	{
		for (const TPair<FName, TObjectPtr<UClothConfigBase>>& ClothConfig : ClothConfigs)
		{
			if (ClothConfig.Value->NeedsTethers())
			{
				if (ClothConfig.Value->TethersUseGeodesicDistance())
				{
					bThethersUseGeodesicDistance = true;
				}
				else
				{
					bThethersUseEuclideanDistance = true;
				}
			}
		}
	}

	// Recalculate cached data
	bool bHasClothChanged = false;
	for (FClothLODDataCommon& CurrentLodData : LodData)
	{
		FClothPhysicalMeshData& PhysMesh = CurrentLodData.PhysicalMeshData;

		if (bNeedsInverseMasses)
		{
			PhysMesh.CalculateInverseMasses();
			bHasClothChanged = true;
		}

		if (bNeedsNumInfluences)
		{
			PhysMesh.CalculateNumInfluences();
			bHasClothChanged = true;
		}

		if (bNeedsSelfCollisionData)
		{
			PhysMesh.BuildSelfCollisionData(SelfCollisionRadius);
			bHasClothChanged = true;
		}

		if (bNeedsTethers)
		{
			PhysMesh.CalculateTethers(bThethersUseEuclideanDistance, bThethersUseGeodesicDistance);
			bHasClothChanged = true;
		}
	}

	// Inform the simulations that the cloth has changed
	if (bHasClothChanged)
	{
		ForEachInteractorUsingClothing([](UClothingSimulationInteractor* InInteractor)
		{
			if (InInteractor)
			{
				InInteractor->ClothConfigUpdated();
			}
		});
	}
}
#endif // #if WITH_EDITORONLY_DATA

#if WITH_EDITOR
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
		const FName& PropertyName = ChainEvent.PropertyChain.GetActiveMemberNode() && ChainEvent.PropertyChain.GetActiveMemberNode()->GetNextNode() ?
			ChainEvent.PropertyChain.GetActiveMemberNode()->GetNextNode()->GetValue()->GetFName() : NAME_None;

		if (PropertyName == FName("SelfCollisionRadius") ||
			PropertyName == FName("SelfCollisionCullScale"))
		{
			InvalidateFlaggedCachedData(EClothingCachedDataFlagsCommon::SelfCollisionData);
			bReregisterComponents = true;
		}
		else if (PropertyName == FName("bUseGeodesicDistance"))
		{
			InvalidateFlaggedCachedData(EClothingCachedDataFlagsCommon::Tethers);
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
