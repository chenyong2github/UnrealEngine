// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyPathHelpers.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "Rigs/RigHierarchyCache.h"
#include "Stats/StatsHierarchical.h"
#include "RigVMCore/RigVMExecuteContext.h"
#include "ControlRigDefines.generated.h"

#define DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT() \
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

USTRUCT()
struct FControlRigExecuteContext : public FRigVMExecuteContext
{
	GENERATED_BODY()

	FControlRigExecuteContext()
		: FRigVMExecuteContext()
		, Hierarchy(nullptr)
	{
	}
		
	FRigHierarchyContainer* Hierarchy;

	FRigBoneHierarchy* GetBones()
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->BoneHierarchy;
		}
		return nullptr;
	}

	FRigSpaceHierarchy* GetSpaces()
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->SpaceHierarchy;
		}
		return nullptr;
	}

	FRigControlHierarchy* GetControls()
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->ControlHierarchy;
		}
		return nullptr;
	}

	FRigCurveContainer* GetCurves()
	{
		if (Hierarchy != nullptr)
		{
			return &Hierarchy->CurveContainer;
		}
		return nullptr;
	}
};

UENUM()
enum class ETransformSpaceMode : uint8
{
	/** Apply in parent space */
	LocalSpace,

	/** Apply in rig space*/
	GlobalSpace,

	/** Apply in Base space */
	BaseSpace,

	/** Apply in base bone */
	BaseJoint,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

UENUM()
namespace EControlRigClampSpatialMode
{
	enum Type
	{
		Plane,
		Cylinder,
		Sphere
	};
}

UENUM()
enum class ETransformGetterType : uint8
{
	Initial,
	Current,
	Max UMETA(Hidden),
};

UENUM()
enum class EBoneGetterSetterMode : uint8
{
	/** Apply in parent space */
	LocalSpace,

	/** Apply in rig space*/
	GlobalSpace,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

// thought of mixing this with execution on
// the problem is execution on is transient state, and 
// this execution type is something to be set per rig
UENUM()
enum class ERigExecutionType : uint8
{
	Runtime,
	Editing, // editing time
	Max UMETA(Hidden),
};
