// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

DECLARE_DELEGATE_TwoParams(FBakeToControlDelegate,
bool, float);

struct BakeToControlRigDialog
{
	static void GetBakeParams(FBakeToControlDelegate& Delegate);

};