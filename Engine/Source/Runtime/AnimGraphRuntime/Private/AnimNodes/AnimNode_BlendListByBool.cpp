// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNodes/AnimNode_BlendListByBool.h"

/////////////////////////////////////////////////////
// FAnimNode_BlendListByBool

int32 FAnimNode_BlendListByBool::GetActiveChildIndex()
{
	// Note: Intentionally flipped boolean sense (the true input is #0, and the false input is #1)
	return GetActiveValue() ? 0 : 1;
}

bool FAnimNode_BlendListByBool::GetActiveValue() const
{
	return GET_ANIM_NODE_DATA(bool, bActiveValue);
}