// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ClothLODData.h"


UClothLODDataBase::UClothLODDataBase(const FObjectInitializer& Init)
	: PhysicalMeshData(nullptr)
{}

UClothLODDataBase::~UClothLODDataBase()
{}

#if WITH_EDITORONLY_DATA
void UClothLODDataBase::GetParameterMasksForTarget(
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

void UClothLODDataBase::Serialize(FArchive& Ar)
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

#if WITH_EDITOR
void UClothLODDataBase::PushWeightsToMesh()
{
	if (PhysicalMeshData)
	{
		PhysicalMeshData->ClearParticleParameters();
		for (const FPointWeightMap &Weights : ParameterMasks)
		{
			if (Weights.bEnabled)
			{
				if (TArray<float>* TargetArray = PhysicalMeshData->GetFloatArray(Weights.CurrentTarget))
				{
					*TargetArray = Weights.GetValueArray();
				}
			}
		}
	}
}
#endif
