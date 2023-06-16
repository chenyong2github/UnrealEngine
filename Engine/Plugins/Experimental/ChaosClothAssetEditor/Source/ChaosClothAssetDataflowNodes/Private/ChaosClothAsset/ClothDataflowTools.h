// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;
class FSkeletalMeshLODModel;
class FString;

namespace UE::Chaos::ClothAsset
{
	/**
	* Tools shared by cloth data flow nodes
	*/
	struct FClothDataflowTools
	{
		static void AddRenderPatternFromSkeletalMeshSection(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FSkeletalMeshLODModel& SkeletalMeshModel, const int32 SectionIndex, const FString& RenderMaterialPathName);

		static void AddSimPatternsFromSkeletalMeshSection(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FSkeletalMeshLODModel& SkeletalMeshModel, const int32 SectionIndex, const int32 UVChannelIndex);
	};
}  // End namespace UE::Chaos::ClothAsset
