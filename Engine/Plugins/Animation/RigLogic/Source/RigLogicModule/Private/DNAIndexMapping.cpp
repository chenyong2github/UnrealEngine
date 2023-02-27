// Copyright Epic Games, Inc. All Rights Reserved.

#include "DNAIndexMapping.h"

#include "HAL/LowLevelMemTracker.h"
#include "DNAReader.h"
#include "Animation/Skeleton.h"
#include "Animation/SmartName.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimCurveTypes.h"


/** Constructs curve name from nameToSplit using formatString of form x<obj>y<attr>z **/
static FString CreateCurveName(const FString& NameToSplit, const FString& FormatString)
{
	// constructs curve name from NameToSplit (always in form <obj>.<attr>)
	// using FormatString of form x<obj>y<attr>z
	// where x, y and z are arbitrary strings
	// example:
	// FormatString="mesh_<obj>_<attr>"
	// 'head.blink_L' becomes 'mesh_head_blink_L'
	FString ObjectName, AttributeName;
	if (!NameToSplit.Split(".", &ObjectName, &AttributeName))
	{
		return TEXT("");
	}
	FString CurveName = FormatString;
	CurveName = CurveName.Replace(TEXT("<obj>"), *ObjectName);
	CurveName = CurveName.Replace(TEXT("<attr>"), *AttributeName);
	return CurveName;
}

void FDNAIndexMapping::MapControlCurves(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	const uint32 ControlCount = DNABehavior->GetRawControlCount();

	ControlAttributeCurves.Empty();
	ControlAttributeCurves.Reserve(ControlCount);

	for (uint32_t ControlIndex = 0; ControlIndex < ControlCount; ++ControlIndex)
	{
		const FString DNAControlName = DNABehavior->GetRawControlName(ControlIndex);
		const FString AnimatedControlName = CreateCurveName(DNAControlName, TEXT("<obj>_<attr>"));
		if (AnimatedControlName == TEXT(""))
		{
			return;
		}
		ControlAttributeCurves.Add(*AnimatedControlName, ControlIndex);
	}
}

void FDNAIndexMapping::MapJoints(const IBehaviorReader* DNABehavior, const USkeletalMeshComponent* SkeletalMeshComponent)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	const uint16 JointCount = DNABehavior->GetJointCount();
	JointsMapDNAIndicesToMeshPoseBoneIndices.Reset(JointCount);
	for (uint16 JointIndex = 0; JointIndex < JointCount; ++JointIndex)
	{
		const FString JointName = DNABehavior->GetJointName(JointIndex);
		const FName BoneName = FName(*JointName);
		const int32 BoneIndex = SkeletalMeshComponent->GetBoneIndex(BoneName);
		// BoneIndex may be INDEX_NONE, but it's handled properly by the Evaluate method
		JointsMapDNAIndicesToMeshPoseBoneIndices.Add(FMeshPoseBoneIndex{BoneIndex});
	}
}

void FDNAIndexMapping::MapMorphTargets(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton, const USkeletalMesh* SkeletalMesh)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	const uint16 LODCount = DNABehavior->GetLODCount();
	const TMap<FName, int32>& MorphTargetIndexMap = SkeletalMesh->GetMorphTargetIndexMap();
	const TArray<UMorphTarget*>& MorphTargets = SkeletalMesh->GetMorphTargets();

	MorphTargetCurvesPerLOD.Reset(LODCount);
	MorphTargetCurvesPerLOD.AddDefaulted(LODCount);

	for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		TArrayView<const uint16> MappingIndicesForLOD = DNABehavior->GetMeshBlendShapeChannelMappingIndicesForLOD(LODIndex);

		MorphTargetCurvesPerLOD[LODIndex].Reserve(MappingIndicesForLOD.Num());

		for (uint16 MappingIndex : MappingIndicesForLOD)
		{
			const FMeshBlendShapeChannelMapping Mapping = DNABehavior->GetMeshBlendShapeChannelMapping(MappingIndex);
			const FString MeshName = DNABehavior->GetMeshName(Mapping.MeshIndex);
			const FString BlendShapeName = DNABehavior->GetBlendShapeChannelName(Mapping.BlendShapeChannelIndex);
			const FString MorphTargetStr = MeshName + TEXT("__") + BlendShapeName;
			const FName MorphTargetName(*MorphTargetStr);
			const int32* MorphTargetIndex = MorphTargetIndexMap.Find(MorphTargetName);
			if ((MorphTargetIndex != nullptr) && (*MorphTargetIndex != INDEX_NONE))
			{
				const UMorphTarget* MorphTarget = MorphTargets[*MorphTargetIndex];
				MorphTargetCurvesPerLOD[LODIndex].Add(MorphTarget->GetFName(), Mapping.BlendShapeChannelIndex);
			}
		}
	}
}

void FDNAIndexMapping::MapMaskMultipliers(const IBehaviorReader* DNABehavior, const USkeleton* Skeleton)
{
	LLM_SCOPE_BYNAME(TEXT("Animation/RigLogic"));

	const uint16 LODCount = DNABehavior->GetLODCount();

	MaskMultiplierCurvesPerLOD.Reset();
	MaskMultiplierCurvesPerLOD.AddDefaulted(LODCount);

	for (uint16 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		TArrayView<const uint16> IndicesPerLOD = DNABehavior->GetAnimatedMapIndicesForLOD(LODIndex);

		MaskMultiplierCurvesPerLOD[LODIndex].Reserve(IndicesPerLOD.Num());

		for (uint16 AnimMapIndex : IndicesPerLOD)
		{
			const FString AnimatedMapName = DNABehavior->GetAnimatedMapName(AnimMapIndex);
			const FString MaskMultiplierNameStr = CreateCurveName(AnimatedMapName, TEXT("<obj>_<attr>"));
			if (MaskMultiplierNameStr == "")
			{
				return;
			}

			MaskMultiplierCurvesPerLOD[LODIndex].Add(*MaskMultiplierNameStr, AnimMapIndex);
		}
	}
}
