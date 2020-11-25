// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IKRigHierarchy.cpp: IKRig Data Type impelmentation file
=============================================================================*/

#include "IKRigHierarchy.h"


// @todo: cache this for runtime
TArray<int32> FIKRigHierarchy::FindChildren(int32 Index) const
{
	if (Bones.IsValidIndex(Index))
	{
#if WITH_EDITOR
		return FindIndicesByParentName(Bones[Index].Name);
#else
		return Bones[Index].Children;
#endif 
	}

	return TArray<int32>();
}

