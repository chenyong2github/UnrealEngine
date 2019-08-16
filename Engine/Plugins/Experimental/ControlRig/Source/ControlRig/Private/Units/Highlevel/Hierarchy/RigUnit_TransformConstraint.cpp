// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RigUnit_TransformConstraint.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"
#include "AnimationCoreLibrary.h"

void FRigUnit_TransformConstraint::Execute(const FRigUnitContext& Context)
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyRef& HierarchyRef = ExecuteContext.HierarchyReference;

	if (Context.State == EControlRigState::Init)
	{
		ConstraintData.Reset();
		ConstraintDataToTargets.Reset();

		FRigHierarchy* Hierarchy = HierarchyRef.Get();
		if (Hierarchy)
		{
			int32 BoneIndex = Hierarchy->GetIndex(Bone);
			if (BoneIndex != INDEX_NONE)
			{
				const int32 TargetNum = Targets.Num();
				if (TargetNum > 0)
				{
					const FTransform SourceTransform = Hierarchy->GetGlobalTransform(BoneIndex);
					FTransform InputBaseTransform = UtilityHelpers::GetBaseTransformByMode(BaseTransformSpace, [Hierarchy](const FName& JointName) { return Hierarchy->GetGlobalTransform(JointName); },
						Hierarchy->GetBones()[BoneIndex].ParentName, BaseBone, BaseTransform);
					for (int32 TargetIndex = 0; TargetIndex < TargetNum; ++TargetIndex)
					{
						// talk to Rob about the implication of support both of this
						const bool bTranslationFilterValid = Targets[TargetIndex].Filter.TranslationFilter.IsValid();
						const bool bRotationFilterValid = Targets[TargetIndex].Filter.RotationFilter.IsValid();
						const bool bScaleFilterValid = Targets[TargetIndex].Filter.ScaleFilter.IsValid();

						if (bTranslationFilterValid && bRotationFilterValid && bScaleFilterValid)
						{
							AddConstraintData(ETransformConstraintType::Parent, TargetIndex, SourceTransform, InputBaseTransform);
						}
						else
						{
							if (bTranslationFilterValid)
							{
								AddConstraintData(ETransformConstraintType::Translation, TargetIndex, SourceTransform, InputBaseTransform);
							}

							if (bRotationFilterValid)
							{
								AddConstraintData(ETransformConstraintType::Rotation, TargetIndex, SourceTransform, InputBaseTransform);
							}

							if (bScaleFilterValid)
							{
								AddConstraintData(ETransformConstraintType::Scale, TargetIndex, SourceTransform, InputBaseTransform);
							}
						}
					}
				}
			}
		}
	}
	else if (Context.State == EControlRigState::Update)
	{
		FRigHierarchy* Hierarchy = HierarchyRef.Get();
		if (Hierarchy)
		{
			int32 BoneIndex = Hierarchy->GetIndex(Bone);
			if (BoneIndex != INDEX_NONE)
			{
				const int32 TargetNum = Targets.Num();
				if (TargetNum > 0 && ConstraintData.Num() > 0)
				{
					for (int32 ConstraintIndex= 0; ConstraintIndex< ConstraintData.Num(); ++ConstraintIndex)
					{
						// for now just try translate
						const int32 TargetIndex = *ConstraintDataToTargets.Find(ConstraintIndex);
						ConstraintData[ConstraintIndex].CurrentTransform = Targets[TargetIndex].Transform;
						ConstraintData[ConstraintIndex].Weight = Targets[TargetIndex].Weight;
					}

					FTransform InputBaseTransform = UtilityHelpers::GetBaseTransformByMode(BaseTransformSpace, [Hierarchy](const FName& JointName) { return Hierarchy->GetGlobalTransform(JointName); },
							Hierarchy->GetBones()[BoneIndex].ParentName, BaseBone, BaseTransform);

					FTransform SourceTransform = Hierarchy->GetGlobalTransform(BoneIndex);

					// @todo: ignore maintain offset for now
					FTransform ConstrainedTransform = AnimationCore::SolveConstraints(SourceTransform, InputBaseTransform, ConstraintData);

					Hierarchy->SetGlobalTransform(BoneIndex, ConstrainedTransform);
				}
			}
		}
	}
}

void FRigUnit_TransformConstraint::AddConstraintData(ETransformConstraintType ConstraintType, const int32 TargetIndex, const FTransform& SourceTransform, const FTransform& InBaseTransform)
{
	const FConstraintTarget& Target = Targets[TargetIndex];

	int32 NewIndex = ConstraintData.AddDefaulted();
	check(NewIndex != INDEX_NONE);
	FConstraintData& NewData = ConstraintData[NewIndex];
	NewData.Constraint = FTransformConstraintDescription(ConstraintType);
	NewData.bMaintainOffset = Target.bMaintainOffset;
	NewData.Weight = Target.Weight;

	if (Target.bMaintainOffset)
	{
		NewData.SaveInverseOffset(SourceTransform, Target.Transform, InBaseTransform);
	}

	ConstraintDataToTargets.Add(NewIndex, TargetIndex);
}