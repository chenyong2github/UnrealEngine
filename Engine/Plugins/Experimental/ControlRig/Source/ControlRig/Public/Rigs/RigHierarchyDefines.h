// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FRigHierarchyContainer;

UENUM()
enum class ERigHierarchyElementType : uint8
{
	Bone,
	Space,
	Control,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigHierarchyElementChanged, FRigHierarchyContainer*, ERigHierarchyElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigHierarchyElementAdded, FRigHierarchyContainer*, ERigHierarchyElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigHierarchyElementRemoved, FRigHierarchyContainer*, ERigHierarchyElementType, const FName&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigHierarchyElementRenamed, FRigHierarchyContainer*, ERigHierarchyElementType, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_FiveParams(FRigHierarchyElementReparented, FRigHierarchyContainer*, ERigHierarchyElementType, const FName&, const FName&, const FName&);
