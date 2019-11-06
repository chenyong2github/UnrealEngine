// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Enum for each bone in the SteamVR hand skeleton
*/
enum ESteamVRBone : int8
{
	ESteamVRBone_Root = 0,
	ESteamVRBone_Wrist,
	ESteamVRBone_Thumb0,
	ESteamVRBone_Thumb1,
	ESteamVRBone_Thumb2,
	ESteamVRBone_Thumb3,
	ESteamVRBone_IndexFinger0,
	ESteamVRBone_IndexFinger1,
	ESteamVRBone_IndexFinger2,
	ESteamVRBone_IndexFinger3,
	ESteamVRBone_IndexFinger4,
	ESteamVRBone_MiddleFinger0,
	ESteamVRBone_MiddleFinger1,
	ESteamVRBone_MiddleFinger2,
	ESteamVRBone_MiddleFinger3,
	ESteamVRBone_MiddleFinger4,
	ESteamVRBone_RingFinger0,
	ESteamVRBone_RingFinger1,
	ESteamVRBone_RingFinger2,
	ESteamVRBone_RingFinger3,
	ESteamVRBone_RingFinger4,
	ESteamVRBone_PinkyFinger0,
	ESteamVRBone_PinkyFinger1,
	ESteamVRBone_PinkyFinger2,
	ESteamVRBone_PinkyFinger3,
	ESteamVRBone_PinkyFinger4,
	ESteamVRBone_Aux_Thumb,
	ESteamVRBone_Aux_IndexFinger,
	ESteamVRBone_Aux_MiddleFinger,
	ESteamVRBone_Aux_RingFinger,
	ESteamVRBone_Aux_PinkyFinger,
	ESteamVRBone_Count
};


/**
 * Convenience functions to access static data as defined 
 * by the SteamVR Skeletal Input skeleton
*/
namespace SteamVRSkeleton
{
	/** Returns the number of bones in the skeleton */
	inline int32	GetBoneCount() { return ESteamVRBone_Count; }

	/** Returns the name of the bone at the given index */
	const FName&	GetBoneName(int32 nBoneIndex);

	/** Returns the index of the parent bone of the given bone.  Returns -1 if the bone does not have a parent */
	int32			GetParentIndex(int32 nBoneIndex);

	/** Returns the number of children of the given bone */
	int32			GetChildCount(int32 nBoneIndex);

	/** Returns the index of the nth child of the given bone */
	int32			GetChildIndex(int32 nBoneIndex, int32 nChildIndex);
};
