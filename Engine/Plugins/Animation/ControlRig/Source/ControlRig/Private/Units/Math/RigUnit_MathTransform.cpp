// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Math/RigUnit_MathTransform.h"
#include "Units/Math/RigUnit_MathVector.h"
#include "Math/ControlRigMathLibrary.h"
#include "Units/RigUnitContext.h"
#include "AnimationCoreLibrary.h"

FRigUnit_MathTransformFromEulerTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = EulerTransform.ToFTransform();
}

FRigUnit_MathTransformToEulerTransform_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result.FromFTransform(Value);
}

FRigUnit_MathTransformMul_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = A * B;
}

FRigUnit_MathTransformMakeRelative_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Local = Global.GetRelativeTransform(Parent);
	Local.NormalizeRotation();
}

FRigUnit_MathTransformMakeAbsolute_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Global = Local * Parent;
	Global.NormalizeRotation();
}

FString FRigUnit_MathTransformAccumulateArray::GetUnitLabel() const
{
	static const FString RelativeLabel = TEXT("Make Transform Array Relative");
	static const FString AbsoluteLabel = TEXT("Make Transform Array Absolute");
	return (TargetSpace == EBoneGetterSetterMode::GlobalSpace) ? AbsoluteLabel : RelativeLabel;
}

FRigUnit_MathTransformAccumulateArray_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	if(Transforms.Num() == 0)
	{
		return;
	}

	if(ParentIndices.Num() > 0 && ParentIndices.Num() != Transforms.Num())
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_ERROR(TEXT("If the indices are specified their num (%d) has to match the transforms (%d)."), ParentIndices.Num(), Transforms.Num());
		return;
	}

	if(TargetSpace == EBoneGetterSetterMode::LocalSpace)
	{
		if(ParentIndices.IsEmpty())
		{
			for(int32 Index=Transforms.Num()-1; Index>=0;Index--)
			{
				const FTransform& ParentTransform = (Index == 0) ? Root : Transforms[Index - 1];
				Transforms[Index] = Transforms[Index].GetRelativeTransform(ParentTransform);
			}
		}
		else
		{
			for(int32 Index=Transforms.Num()-1; Index>=0;Index--)
			{
				const int32 ParentIndex = ParentIndices[Index];
				const FTransform& ParentTransform = (ParentIndex == INDEX_NONE || ParentIndex >= Index) ? Root : Transforms[ParentIndex];
				Transforms[Index] = Transforms[Index].GetRelativeTransform(ParentTransform);
			}
		}
	}
	else
	{
		if(ParentIndices.IsEmpty())
		{
			for(int32 Index=0; Index<Transforms.Num(); Index++)
			{
				const FTransform& ParentTransform = (Index == 0) ? Root : Transforms[Index - 1];
				Transforms[Index] = Transforms[Index] * ParentTransform;
			}
		}
		else
		{
			for(int32 Index=0; Index<Transforms.Num(); Index++)
			{
				const int32 ParentIndex = ParentIndices[Index];
				const FTransform& ParentTransform = (ParentIndex == INDEX_NONE || ParentIndex >= Index) ? Root : Transforms[ParentIndex];
				Transforms[Index] = Transforms[Index] * ParentTransform;
			}
		}
	}
}

FRigUnit_MathTransformInverse_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Value.Inverse();
}

FRigUnit_MathTransformLerp_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = FControlRigMathLibrary::LerpTransform(A, B, T);
}

FRigUnit_MathTransformSelectBool_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Condition ? IfTrue : IfFalse;
}

FRigUnit_MathTransformRotateVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformVector(Direction);
}

FRigUnit_MathTransformTransformVector_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Result = Transform.TransformPosition(Location);
}

FRigUnit_MathTransformFromSRT_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	Transform.SetLocation(Location);
	Transform.SetRotation(AnimationCore::QuatFromEuler(Rotation, RotationOrder));
	Transform.SetScale3D(Scale);
	EulerTransform.FromFTransform(Transform);
}

FRigUnit_MathTransformClampSpatially_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FVector Position;
	FRigUnit_MathVectorClampSpatially::StaticExecute(RigVMExecuteContext, Value.GetTranslation(), Axis, Type, Minimum, Maximum, Space, bDrawDebug, DebugColor, DebugThickness, Position, Context);
	Result = Value;
	Result.SetTranslation(Position);
}