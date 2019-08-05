// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "RigVMNotifications.generated.h"

UENUM()
enum class ERigVMGraphNotifType : uint8
{
	GraphChanged,
	NodeAdded,
	NodeRemoved,
	NodeSelected,
	NodeDeselected,
	Invalid
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigVMGraphModifiedEvent, ERigVMGraphNotifType /* type */, URigVMGraph* /* graph */, UObject* /* subject */);