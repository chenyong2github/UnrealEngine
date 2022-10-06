// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/ClothPresetCollection.h"
#include "Chaos/ChaosArchive.h"

namespace UE::Chaos::ClothAsset
{
	const FName FClothPresetCollection::PropertyGroup("Property");

	FClothPresetCollection::FClothPresetCollection()
	{
		Construct();
	}

	void FClothPresetCollection::Serialize(FArchive& Ar)
	{
		::Chaos::FChaosArchive ChaosArchive(Ar);
		Super::Serialize(ChaosArchive);
	}

	void FClothPresetCollection::Construct()
	{
		// Property Group
		AddExternalAttribute<FString>("Name", PropertyGroup, Name);
		AddExternalAttribute<FVector3f>("LowValue", PropertyGroup, LowValue);
		AddExternalAttribute<FVector3f>("HighValue", PropertyGroup, HighValue);
		AddExternalAttribute<FString>("StringValue", PropertyGroup, StringValue);
		AddExternalAttribute<bool>("Enable", PropertyGroup, Enable);
		AddExternalAttribute<bool>("Animatable", PropertyGroup, Animatable);
	}
}  // End namespace UE::Chaos::ClothAsset
