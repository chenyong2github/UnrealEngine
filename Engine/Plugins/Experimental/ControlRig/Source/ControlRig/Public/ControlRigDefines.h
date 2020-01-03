// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyPathHelpers.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigCurveContainer.h"
#include "Stats/StatsHierarchical.h"
#include "ControlRigDefines.generated.h"

#define DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
//	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

USTRUCT()
struct FControlRigExecuteContext
{
	GENERATED_BODY()

	FControlRigExecuteContext()
		: Hierarchy(nullptr)
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

UENUM()
enum class EControlRigOpCode : uint8
{
	Done,
	Copy,
	Exec,
	Invalid,
};

USTRUCT()
struct FControlRigOperator
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "FControlRigBlueprintOperator")
	EControlRigOpCode OpCode;

	/** Path to the property we are linking from */
	UPROPERTY()
	FString PropertyPath1_DEPRECATED;

	/** Path to the property we are linking to */
	UPROPERTY()
	FString PropertyPath2_DEPRECATED;

	/** Path to the property we are linking from */
	UPROPERTY(VisibleAnywhere, Category = "FControlRigBlueprintOperator")
	FCachedPropertyPath CachedPropertyPath1;

	/** Path to the property we are linking to */
	UPROPERTY(VisibleAnywhere, Category = "FControlRigBlueprintOperator")
	FCachedPropertyPath CachedPropertyPath2;

	FControlRigOperator(EControlRigOpCode Op = EControlRigOpCode::Invalid)
		: OpCode(Op)
	{
	}

	FControlRigOperator(EControlRigOpCode Op, const FCachedPropertyPath& InProperty1, const FCachedPropertyPath& InProperty2)
		: OpCode(Op)
		, CachedPropertyPath1(InProperty1)
		, CachedPropertyPath2(InProperty2)
	{

	}

	static FControlRigOperator MakeUnresolvedCopy(const FControlRigOperator& ToCopy);

	bool Resolve(UObject* OuterObject);

	FString ToString()
	{
		TArray<FStringFormatArg> Arguments;
		Arguments.Add(FStringFormatArg((int32)OpCode));
		Arguments.Add(CachedPropertyPath1.ToString());
		Arguments.Add(CachedPropertyPath1.ToString());

		return FString::Format(TEXT("Opcode {0} : Property1 {1}, Property2 {2}"), Arguments);
	}
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
