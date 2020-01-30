// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClothLODData.h"
#include "ClothPhysicalMeshData.h"

UClothLODDataCommon::UClothLODDataCommon(const FObjectInitializer& Init)
	: Super(Init)
	, PhysicalMeshData_DEPRECATED(nullptr)
{
}

UClothLODDataCommon::~UClothLODDataCommon()
{}

#if WITH_EDITORONLY_DATA
void UClothLODDataCommon::GetParameterMasksForTarget(
	const uint8 InTarget, 
	TArray<FPointWeightMap*>& OutMasks)
{
	for(FPointWeightMap& Mask : ParameterMasks)
	{
		if(Mask.CurrentTarget == InTarget)
		{
			OutMasks.Add(&Mask);
		}
	}
}
#endif // WITH_EDITORONLY_DATA

void UClothLODDataCommon::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	// Ryan - do we need to do this tagged property business still?

	// Serialize normal tagged data
	//UScriptStruct* Struct = UClothLODDataNv::StaticStruct();
	//UClass* Class = UClothLODDataNv::StaticClass();
	/*if (!Ar.IsCountingMemory())
	{
		//Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);
		Class->SerializeTaggedProperties(Ar, (uint8*)this, Class, nullptr);
	}*/

	// Serialize the mesh to mesh data (not a USTRUCT)
	Ar << TransitionUpSkinData
	   << TransitionDownSkinData;
}

void UClothLODDataCommon::PostLoad()
{
	Super::PostLoad();

	if (PhysicalMeshData_DEPRECATED)
	{
		ClothPhysicalMeshData.MigrateFrom(PhysicalMeshData_DEPRECATED);
		PhysicalMeshData_DEPRECATED = nullptr;
	}
}

#if WITH_EDITOR
void UClothLODDataCommon::PushWeightsToMesh()
{
	ClothPhysicalMeshData.ClearWeightMaps();
	for (const FPointWeightMap& Weights : ParameterMasks)
	{
		if (Weights.bEnabled)
		{
			FPointWeightMap& TargetWeightMap = ClothPhysicalMeshData.FindOrAddWeightMap(Weights.CurrentTarget);
			TargetWeightMap.Values = Weights.Values;
		}
	}
}
#endif
