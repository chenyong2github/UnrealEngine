// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BoneIndices.h"
#include "FABRIK.generated.h"

struct FBoneContainer;
/**
*	Controller which implements the FABRIK IK approximation algorithm -  see http://andreasaristidou.com/publications/FABRIK.pdf for details
*/

/** Transient structure for FABRIK node evaluation */

/** Transient structure for FABRIK node evaluation */
USTRUCT()
struct FFABRIKChainLink
{
	GENERATED_USTRUCT_BODY()

public:
	/** Position of bone in component space. */
	FVector Position;

	/** Distance to its parent link. */
	float Length;

	/** Bone Index in SkeletalMesh */
	int32 BoneIndex;

	/** Transform Index that this control will output */
	int32 TransformIndex;

	/** Default Direction to Parent */
	FVector DefaultDirToParent;

	/** Child bones which are overlapping this bone.
	* They have a zero length distance, so they will inherit this bone's transformation. */
	TArray<int32> ChildZeroLengthTransformIndices;

	FFABRIKChainLink()
		: Position(FVector::ZeroVector)
		, Length(0.f)
		, BoneIndex(INDEX_NONE)
		, TransformIndex(INDEX_NONE)
		, DefaultDirToParent(FVector(-1.f, 0.f, 0.f))
	{
	}

	FFABRIKChainLink(const FVector& InPosition, const float InLength, const FCompactPoseBoneIndex& InBoneIndex, const int32& InTransformIndex)
		: Position(InPosition)
		, Length(InLength)
		, BoneIndex(InBoneIndex.GetInt())
		, TransformIndex(InTransformIndex)
		, DefaultDirToParent(FVector(-1.f, 0.f, 0.f))
	{
	}

	FFABRIKChainLink(const FVector& InPosition, const float InLength, const FCompactPoseBoneIndex& InBoneIndex, const int32& InTransformIndex, const FVector& InDefaultDirToParent)
		: Position(InPosition)
		, Length(InLength)
		, BoneIndex(InBoneIndex.GetInt())
		, TransformIndex(InTransformIndex)
		, DefaultDirToParent(InDefaultDirToParent)
	{
	}

	FFABRIKChainLink(const FVector& InPosition, const float InLength, const int32 InBoneIndex, const int32 InTransformIndex)
		: Position(InPosition)
		, Length(InLength)
		, BoneIndex(InBoneIndex)
		, TransformIndex(InTransformIndex)
		, DefaultDirToParent(FVector(-1.f, 0.f, 0.f))
	{
	}

	static ANIMATIONCORE_API FVector GetDirectionToParent(const FBoneContainer& BoneContainer, FCompactPoseBoneIndex BoneIndex);
};

namespace AnimationCore
{
	/**
	* Fabrik solver
	*
	* This solves FABRIK
	*
	* @param	Chain				Array of chain data
	* @param	TargetPosition		Target for the IK
	* @param	MaximumReach		Maximum Reach
	* @param	Precision			Precision
	* @param	MaxIteration		Number of Max Iteration
	*
	* @return  true if modified. False if not.
	*/
	ANIMATIONCORE_API bool SolveFabrik(TArray<FFABRIKChainLink>& InOutChain, const FVector& TargetPosition, float MaximumReach, float Precision, int32 MaxIteration);
};
