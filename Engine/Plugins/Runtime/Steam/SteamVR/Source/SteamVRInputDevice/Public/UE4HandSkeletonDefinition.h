// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Enum for each bone in the UE4 hand skeleton
*/
enum EUE4HandBone : int8
{
	EUE4HandBone_Wrist = 0,
	EUE4HandBone_Index_01,
	EUE4HandBone_Index_02,
	EUE4HandBone_Index_03,
	EUE4HandBone_Middle_01,
	EUE4HandBone_Middle_02,
	EUE4HandBone_Middle_03,
	EUE4HandBone_Pinky_01,
	EUE4HandBone_Pinky_02,
	EUE4HandBone_Pinky_03,
	EUE4HandBone_Ring_01,
	EUE4HandBone_Ring_02,
	EUE4HandBone_Ring_03,
	EUE4HandBone_Thumb_01,
	EUE4HandBone_Thumb_02,
	EUE4HandBone_Thumb_03,
	EUE4HandBone_Count
};


/**
 * Convenience functions to access static data as defined 
 * by the UE4 VR reference hand skeleton
*/
namespace UE4HandSkeleton
{
	/** Returns the number of bones in the skeleton */
	inline int32	GetBoneCount() { return EUE4HandBone_Count; }

	/** Returns the name of the bone at the given index */
	const FName&	GetBoneName(int32 nBoneIndex);

	/** Returns the index of the parent bone of the given bone.  Returns -1 if the bone does not have a parent */
	int32			GetParentIndex(int32 nBoneIndex);

	/** Returns the number of children of the given bone */
	int32			GetChildCount(int32 nBoneIndex);

	/** Returns the index of the nth child of the given bone */
	int32			GetChildIndex(int32 nBoneIndex, int32 nChildIndex);
};
