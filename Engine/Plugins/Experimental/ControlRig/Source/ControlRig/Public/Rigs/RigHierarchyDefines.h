// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.generated.h"

struct FRigHierarchyContainer;

UENUM()
enum class ERigElementType : uint8
{
	Bone,
	Space,
	Control,
	Curve,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigElementChanged, FRigHierarchyContainer*, ERigElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigElementAdded, FRigHierarchyContainer*, ERigElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigElementRemoved, FRigHierarchyContainer*, ERigElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigElementRenamed, FRigHierarchyContainer*, ERigElementType, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRigElementReparented, FRigHierarchyContainer*, ERigElementType, const FName&, const FName&, const FName&);
