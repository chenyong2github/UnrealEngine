// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothLODData.h"
#include "ClothPhysicalMeshDataNv.h"
#include "ClothParameterMask_PhysMesh.h"

#include "ClothCollisionData.h"
#include "PointWeightMap.h"

#include "SkeletalMeshTypes.h"
#include "Containers/Array.h"

#include "ClothLODDataNv.generated.h"

/** */
UCLASS()
class CLOTHINGSYSTEMRUNTIMENV_API UClothLODDataNv : public UClothLODDataBase
{
	GENERATED_BODY()
public:
	UClothLODDataNv(const FObjectInitializer& Init);
	virtual ~UClothLODDataNv();
};


/* Deprecated.  Use UClothLODDataNv instead. */
USTRUCT()
struct CLOTHINGSYSTEMRUNTIMENV_API FClothLODData
{
	GENERATED_BODY()

	// Raw phys mesh data
	UPROPERTY(EditAnywhere, Category = SimMesh)
	FClothPhysicalMeshData PhysicalMeshData;

	// Collision primitive and covex data for clothing collisions
	UPROPERTY(EditAnywhere, Category = Collision)
	FClothCollisionData CollisionData;

#if WITH_EDITORONLY_DATA
	// Parameter masks defining the physics mesh masked data
	UPROPERTY(EditAnywhere, Category = Masks)
	TArray<FClothParameterMask_PhysMesh> ParameterMasks;
#endif // WITH_EDITORONLY_DATA

	// Skinning data for transitioning from a higher detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionUpSkinData;

	// Skinning data for transitioning from a lower detail LOD to this one
	TArray<FMeshToMeshVertData> TransitionDownSkinData;

	bool Serialize(FArchive& Ar)
	{
		// Serialize normal tagged data
		if (!Ar.IsCountingMemory())
		{
			UScriptStruct* Struct = FClothLODData::StaticStruct();
			Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
		}
		// Serialize the mesh to mesh data (not a USTRUCT)
		Ar	<< TransitionUpSkinData
			<< TransitionDownSkinData;
		return true;
	}

	void MigrateTo(UClothLODDataNv* LodData) const
	{
		PhysicalMeshData.MigrateTo(LodData->PhysicalMeshData);
		LodData->CollisionData = CollisionData;
#if WITH_EDITORONLY_DATA
		LodData->ParameterMasks.SetNum(ParameterMasks.Num());
		for (int i = 0; i < ParameterMasks.Num(); i++)
		{
			ParameterMasks[i].MigrateTo(&LodData->ParameterMasks[i]);
		}
#endif // WITH_EDITORONLY_DATA
		LodData->TransitionUpSkinData = TransitionUpSkinData;
		LodData->TransitionDownSkinData = TransitionDownSkinData;
	}
};
template<>
struct TStructOpsTypeTraits<FClothLODData> : public TStructOpsTypeTraitsBase2<FClothLODData>
{
	enum
	{
		WithSerializer = true,
	};
};
