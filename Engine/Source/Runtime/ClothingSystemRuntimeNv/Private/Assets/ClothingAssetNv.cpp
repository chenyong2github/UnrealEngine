// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Assets/ClothingAssetNv.h"

#include "PhysXPublic.h"

#if WITH_EDITOR
#include "Engine/SkeletalMesh.h"
#include "MeshUtilities.h"
#endif

#include "Components/SkeletalMeshComponent.h"

#include "PhysicsPublic.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "UObject/AnimPhysObjectVersion.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "ClothingSimulationInteractor.h"
#include "Serialization/CustomVersion.h"

#define LOCTEXT_NAMESPACE "ClothingAsset"

//==============================================================================
// FClothingAssetCustomVersion
//==============================================================================

// Custom serialization version for clothing assets
struct FClothingAssetCustomVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,
		// Added storage of vertex colors with sim data, for editor usage
		AddVertexColorsToPhysicalMesh = 1,
		// Changed memory layouts by moving some properties to base classes
		MovePropertiesToCommonBaseClasses = 2,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

private:
	FClothingAssetCustomVersion() {}
};

const FGuid FClothingAssetCustomVersion::GUID(0xFB680AF2, 0x59EF4BA3, 0xBAA819B5, 0x73C8443D);
FCustomVersionRegistration GRegisterClothingAssetCustomVersion(FClothingAssetCustomVersion::GUID, FClothingAssetCustomVersion::LatestVersion, TEXT("ClothingAssetVer"));

//==============================================================================
// UClothingAssetNv
//==============================================================================

UClothingAssetNv::UClothingAssetNv(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ClothSimConfig = ObjectInitializer.CreateDefaultSubobject<UClothConfigNv>(this, UClothConfigNv::StaticClass()->GetFName());
}

void UClothingAssetNv::PostLoad()
{
	Super::PostLoad();

	const int32 AnimPhysCustomVersion = GetLinkerCustomVersion(FAnimPhysObjectVersion::GUID);
	const int32 ClothingCustomVersion = GetLinkerCustomVersion(FClothingAssetCustomVersion::GUID);

	if (ClothingCustomVersion < FClothingAssetCustomVersion::MovePropertiesToCommonBaseClasses)
	{
		// Remap struct FClothConfig to class UClothConfigNv
		ClothConfig_DEPRECATED.MigrateTo(Cast<UClothConfigNv>(ClothSimConfig));
		// Remap struct FClothLODData to class UClothLODDataNv
		for (FClothLODData &FLod : LodData_DEPRECATED)
		{
			const int32 Idx = AddNewLod();
			FLod.MigrateTo(Cast<UClothLODDataNv>(ClothLodData[Idx]));
		}
		LodData_DEPRECATED.Empty();
	}
	if(AnimPhysCustomVersion < FAnimPhysObjectVersion::AddedClothingMaskWorkflow)
	{
#if WITH_EDITORONLY_DATA
		// Convert current parameters to masks
		for(UClothLODDataBase* LodPtr : ClothLodData)
		{
			check(LodPtr);
			UClothLODDataNv& Lod = *Cast<UClothLODDataNv>(LodPtr);
			check(Lod.PhysicalMeshData);
			UClothPhysicalMeshDataBase& PhysMesh = *Lod.PhysicalMeshData;

			// Didn't do anything previously - clear out incase there's something in there
			// so we can use it correctly now.
			Lod.ParameterMasks.Reset(3);

			// Max distances (Always present)
			Lod.ParameterMasks.AddDefaulted();
			FPointWeightMap& MaxDistanceMask = Lod.ParameterMasks.Last();
			MaxDistanceMask.CopyFrom(PhysMesh.GetFloatArray((uint32)MaskTarget_PhysMesh::MaxDistance), (uint8)MaskTarget_PhysMesh::MaxDistance);
			MaxDistanceMask.bEnabled = true;

			// Following params are only added if necessary, if we don't have any backstop
			// radii then there's no backstops.
			const TArray<float>* BackstopRadiuses = PhysMesh.GetFloatArray((uint32)MaskTarget_PhysMesh::BackstopRadius);
			if(BackstopRadiuses && BackstopRadiuses->FindByPredicate([](const float& A) {return A != 0.0f; }))
			{
				// Backstop radii
				Lod.ParameterMasks.AddDefaulted();
				FPointWeightMap& BackstopRadiusMask = Lod.ParameterMasks.Last();
				BackstopRadiusMask.CopyFrom(PhysMesh.GetFloatArray((uint32)MaskTarget_PhysMesh::BackstopRadius), (uint8)MaskTarget_PhysMesh::BackstopRadius);
				BackstopRadiusMask.bEnabled = true;

				// Backstop distances
				Lod.ParameterMasks.AddDefaulted();
				FPointWeightMap& BackstopDistanceMask = Lod.ParameterMasks.Last();
				BackstopDistanceMask.CopyFrom(PhysMesh.GetFloatArray((uint32)MaskTarget_PhysMesh::BackstopDistance), (uint8)MaskTarget_PhysMesh::BackstopDistance);
				BackstopDistanceMask.bEnabled = true;
			}
			
		}
#endif

		// Make sure we're transactional
		SetFlags(RF_Transactional);
	}

#if WITH_EDITORONLY_DATA
	// Fix content imported before we kept vertex colors
	if(ClothingCustomVersion < FClothingAssetCustomVersion::AddVertexColorsToPhysicalMesh)
	{
		for (UClothLODDataBase* Lod : ClothLodData)
		{
			const int32 NumVerts = Lod->PhysicalMeshData->Vertices.Num(); // number of verts

			Lod->PhysicalMeshData->VertexColors.Reset();
			Lod->PhysicalMeshData->VertexColors.AddUninitialized(NumVerts);
			for (int32 VertIdx = 0; VertIdx < NumVerts; VertIdx++)
			{
				Lod->PhysicalMeshData->VertexColors[VertIdx] = FColor::White;
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITOR
	if(AnimPhysCustomVersion < FAnimPhysObjectVersion::CacheClothMeshInfluences)
	{
		// Rebuild data cache
		InvalidateCachedData();
	}
#endif

	// After fixing the content, we are ready to call functions that rely on it
	BuildSelfCollisionData();
#if WITH_EDITORONLY_DATA
	CalculateReferenceBoneIndex();
#endif
}

void UClothingAssetNv::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FAnimPhysObjectVersion::GUID);
	Ar.UsingCustomVersion(FClothingAssetCustomVersion::GUID);
}

#if WITH_EDITOR

void UClothingAssetNv::InvalidateCachedData()
{
	for(UClothLODDataBase* CurrentLodDataPtr : ClothLodData)
	{
		check(CurrentLodDataPtr);
		UClothLODDataNv& CurrentLodData = *Cast<UClothLODDataNv>(CurrentLodDataPtr);
		// Recalculate inverse masses for the physical mesh particles
		check(CurrentLodData.PhysicalMeshData);
		UClothPhysicalMeshDataBase& PhysMesh = *CurrentLodData.PhysicalMeshData;
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

		const TArray<float>* const MaxDistances = PhysMesh.GetFloatArray((uint32)MaskTarget_PhysMesh::MaxDistance);
		if (MaxDistances && MaxDistances->Num() > 0)
		{
			float MassSum = 0.0f;
			for (int32 CurrVertIndex = 0; CurrVertIndex < NumVerts; ++CurrVertIndex)
			{
				float& InvMass = InvMasses[CurrVertIndex];
				const float& MaxDistance = (*MaxDistances)[CurrVertIndex];

				if (MaxDistance < SMALL_NUMBER)   // For consistency, the default Threshold should be 0.1, not SMALL_NUMBER. But for backward compatibility it needs to be SMALL_NUMBER for now.
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
		}
		else
		{
			// Otherwise, go fully kinematic.
			for(int32 CurrVertIndex = 0; CurrVertIndex < NumVerts; ++CurrVertIndex)
			{
				InvMasses[CurrVertIndex] = 0.0f;
			}
			PhysMesh.NumFixedVerts = NumVerts;
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

int32 UClothingAssetNv::AddNewLod()
{
	const int32 Idx = ClothLodData.AddDefaulted();
	ClothLodData[Idx] = NewObject<UClothLODDataNv>(this, UClothLODDataNv::StaticClass(), NAME_None, RF_Transactional);
	return Idx;
}

void UClothingAssetNv::PostPropertyChangeCb(const FPropertyChangedEvent& Event)
{
	bool bReregisterComponents = false;

	if(Event.ChangeType != EPropertyChangeType::Interactive)
	{
		if(Event.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UClothConfigNv, SelfCollisionRadius) ||
		   Event.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UClothConfigNv, SelfCollisionCullScale))
		{
			BuildSelfCollisionData();
			bReregisterComponents = true;
		}
		else if(Event.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UClothingAssetCommon, PhysicsAsset))
		{
			bReregisterComponents = true;
		}
		else
		{
			// Other properties just require a config refresh
			ForEachInteractorUsingClothing([](UClothingSimulationInteractor* InInteractor)
			{
				if(InInteractor)
				{
					InInteractor->ClothConfigUpdated();
				}
			});
		}
	}

	if(bReregisterComponents)
	{
		ReregisterComponentsUsingClothing();
	}
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
