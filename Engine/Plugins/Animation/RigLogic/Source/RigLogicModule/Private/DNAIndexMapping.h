// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneIndices.h"
#include "Engine/AssetUserData.h"
#include "Animation/AnimCurveTypes.h"

#include "DNAIndexMapping.generated.h"

class IBehaviorReader;
class USkeleton;
class USkeletalMesh;
class USkeletalMeshComponent;

struct FDNAIndexMapping
{
	template <typename T>
	struct TArrayWrapper
	{
		TArray<T> Values;
	};
	
	using FCachedIndexedCurve = TBaseBlendedCurve<FDefaultAllocator, UE::Anim::FCurveElementIndexed>; 

	FGuid SkeletonGuid;

	// all the control attributes that we will need to extract, alongside their control index
	FCachedIndexedCurve ControlAttributeCurves;
	TArray<FMeshPoseBoneIndex> JointsMapDNAIndicesToMeshPoseBoneIndices;
	TArray<FCachedIndexedCurve> MorphTargetCurvesPerLOD;
	TArray<FCachedIndexedCurve> MaskMultiplierCurvesPerLOD;

	void MapControlCurves(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton);
	void MapJoints(const IBehaviorReader* DNABehavior, const USkeletalMeshComponent* SkeletalMeshComponent);
	void MapMorphTargets(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton, const USkeletalMesh* SkeletalMesh);
	void MapMaskMultipliers(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton);

};

UCLASS(NotBlueprintable, hidecategories = (Object), deprecated)
class UDEPRECATED_DNAIndexMapping : public UAssetUserData
{
	GENERATED_BODY()
};