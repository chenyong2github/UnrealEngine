// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigUnit_TransformConstraint.h"
#include "Units/RigUnitContext.h"
#include "HelperUtil.h"
#include "AnimationCoreLibrary.h"

FRigUnit_TransformConstraint_Execute()
{
	FRigUnit_TransformConstraintPerItem::StaticExecute(
		RigVMExecuteContext, 
		FRigElementKey(Bone, ERigElementType::Bone),
		BaseTransformSpace,
		BaseTransform,
		FRigElementKey(BaseBone, ERigElementType::Bone),
		Targets,
		bUseInitialTransforms,
		WorkData,
		ExecuteContext, 
		Context);
}

FRigUnit_TransformConstraintPerItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	TArray<FConstraintData>&	ConstraintData = WorkData.ConstraintData;
	TMap<int32, int32>& ConstraintDataToTargets = WorkData.ConstraintDataToTargets;

	auto SetupConstraintData = [&]()
	{
		ConstraintData.Reset();
		ConstraintDataToTargets.Reset();

		FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
		if(Hierarchy)
		{
			if (Item.IsValid())
			{
				const int32 TargetNum = Targets.Num();
				if (TargetNum > 0)
				{
					const FTransform SourceTransform = bUseInitialTransforms ? Hierarchy->GetInitialGlobalTransform(Item) : Hierarchy->GetGlobalTransform(Item);
					FTransform InputBaseTransform =
						bUseInitialTransforms ?
						UtilityHelpers::GetBaseTransformByMode(
							BaseTransformSpace,
							[Hierarchy](const FRigElementKey& Item) { return Hierarchy->GetInitialGlobalTransform(Item); },
							Hierarchy->GetParentKey(Item),
							BaseItem,
							BaseTransform
						) :
						UtilityHelpers::GetBaseTransformByMode(
							BaseTransformSpace,
							[Hierarchy](const FRigElementKey& Item) { return Hierarchy->GetGlobalTransform(Item); },
							Hierarchy->GetParentKey(Item),
							BaseItem,
							BaseTransform
						);


					for (int32 TargetIndex = 0; TargetIndex < TargetNum; ++TargetIndex)
					{
						// talk to Rob about the implication of support both of this
						const bool bTranslationFilterValid = Targets[TargetIndex].Filter.TranslationFilter.IsValid();
						const bool bRotationFilterValid = Targets[TargetIndex].Filter.RotationFilter.IsValid();
						const bool bScaleFilterValid = Targets[TargetIndex].Filter.ScaleFilter.IsValid();

						if (bTranslationFilterValid && bRotationFilterValid && bScaleFilterValid)
						{
							AddConstraintData(Targets, ETransformConstraintType::Parent, TargetIndex, SourceTransform, InputBaseTransform, ConstraintData, ConstraintDataToTargets);
						}
						else
						{
							if (bTranslationFilterValid)
							{
								AddConstraintData(Targets, ETransformConstraintType::Translation, TargetIndex, SourceTransform, InputBaseTransform, ConstraintData, ConstraintDataToTargets);
							}

							if (bRotationFilterValid)
							{
								AddConstraintData(Targets, ETransformConstraintType::Rotation, TargetIndex, SourceTransform, InputBaseTransform, ConstraintData, ConstraintDataToTargets);
							}

							if (bScaleFilterValid)
							{
								AddConstraintData(Targets, ETransformConstraintType::Scale, TargetIndex, SourceTransform, InputBaseTransform, ConstraintData, ConstraintDataToTargets);
							}
						}
					}
				}
			}
		}
	};

	if (Context.State == EControlRigState::Init)
	{
		ConstraintData.Reset();
		ConstraintDataToTargets.Reset();
		FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
		
		if (Hierarchy)
		{
			SetupConstraintData();
		}
	}
	else if (Context.State == EControlRigState::Update)
	{
		FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
		if (Hierarchy)
		{
			if ((ConstraintData.Num() != Targets.Num()))
			{
				SetupConstraintData();
			}

			if (Item.IsValid())
			{
				const int32 TargetNum = Targets.Num();
				if (TargetNum > 0 && ConstraintData.Num() > 0)
				{
					for (int32 ConstraintIndex= 0; ConstraintIndex< ConstraintData.Num(); ++ConstraintIndex)
					{
						// for now just try translate
						const int32* TargetIndexPtr = ConstraintDataToTargets.Find(ConstraintIndex);
						if (TargetIndexPtr)
						{
							const int32 TargetIndex = *TargetIndexPtr;
							ConstraintData[ConstraintIndex].CurrentTransform = Targets[TargetIndex].Transform;
							ConstraintData[ConstraintIndex].Weight = Targets[TargetIndex].Weight;
						}
					}

					FTransform InputBaseTransform = UtilityHelpers::GetBaseTransformByMode(BaseTransformSpace, [Hierarchy](const FRigElementKey& Item) { return Hierarchy->GetGlobalTransform(Item); },
							Hierarchy->GetParentKey(Item), BaseItem, BaseTransform);

					FTransform SourceTransform = Hierarchy->GetGlobalTransform(Item);

					// @todo: ignore maintain offset for now
					FTransform ConstrainedTransform = AnimationCore::SolveConstraints(SourceTransform, InputBaseTransform, ConstraintData);

					Hierarchy->SetGlobalTransform(Item, ConstrainedTransform);
				}
			}
		}
	}
}

void FRigUnit_TransformConstraintPerItem::AddConstraintData(const FRigVMFixedArray<FConstraintTarget>& Targets, ETransformConstraintType ConstraintType, const int32 TargetIndex, const FTransform& SourceTransform, const FTransform& InBaseTransform, TArray<FConstraintData>& OutConstraintData, TMap<int32, int32>& OutConstraintDataToTargets)
{
	const FConstraintTarget& Target = Targets[TargetIndex];

	int32 NewIndex = OutConstraintData.AddDefaulted();
	check(NewIndex != INDEX_NONE);
	FConstraintData& NewData = OutConstraintData[NewIndex];
	NewData.Constraint = FTransformConstraintDescription(ConstraintType);
	NewData.bMaintainOffset = Target.bMaintainOffset;
	NewData.Weight = Target.Weight;

	if (Target.bMaintainOffset)
	{
		NewData.SaveInverseOffset(SourceTransform, Target.Transform, InBaseTransform);
	}

	OutConstraintDataToTargets.Add(NewIndex, TargetIndex);
}