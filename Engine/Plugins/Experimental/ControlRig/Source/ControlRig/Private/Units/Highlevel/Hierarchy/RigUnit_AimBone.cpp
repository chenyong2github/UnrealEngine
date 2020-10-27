// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_AimBone.h"
#include "Units/RigUnitContext.h"

FRigUnit_AimBoneMath_Execute()
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()

	Result = InputTransform;

	const FRigHierarchyContainer* Hierarchy = Context.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		PrimaryCachedSpace.Reset();
		SecondaryCachedSpace.Reset();
		return;
	}

	if ((Weight <= SMALL_NUMBER) || (Primary.Weight <= SMALL_NUMBER && Secondary.Weight <= SMALL_NUMBER))
	{
		return;
	}

	if (Primary.Weight > SMALL_NUMBER)
	{
		FVector Target = Primary.Target;

		if (PrimaryCachedSpace.UpdateCache(Primary.Space, Hierarchy))
		{
			FTransform Space = Hierarchy->GetGlobalTransform(PrimaryCachedSpace);
			if (Primary.Kind == EControlRigVectorKind::Direction)
			{
				Target = Space.TransformVectorNoScale(Target);
			}
			else
			{
				Target = Space.TransformPositionNoScale(Target);
			}
		}

		if (Context.DrawInterface != nullptr && DebugSettings.bEnabled)
		{
			const FLinearColor Color = FLinearColor(0.f, 1.f, 1.f, 1.f);
			if (Primary.Kind == EControlRigVectorKind::Direction)
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Result.GetLocation(), Result.GetLocation() + Target * DebugSettings.Scale, Color);
			}
			else
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Result.GetLocation(), Target, Color);
				Context.DrawInterface->DrawBox(DebugSettings.WorldOffset, FTransform(FQuat::Identity, Target, FVector(1.f, 1.f, 1.f) * DebugSettings.Scale * 0.1f), Color);
			}
		}

		if (Primary.Kind == EControlRigVectorKind::Location)
		{
			Target = Target - Result.GetLocation();
		}

		if (!Target.IsNearlyZero() && !Primary.Axis.IsNearlyZero())
		{
			Target = Target.GetSafeNormal();
			FVector Axis = Result.TransformVectorNoScale(Primary.Axis).GetSafeNormal();
			float T = Primary.Weight * Weight;
			if (T < 1.f - SMALL_NUMBER)
			{
				Target = FMath::Lerp<FVector>(Axis, Target, T).GetSafeNormal();
			}
			FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
			Result.SetRotation((Rotation * Result.GetRotation()).GetNormalized());
		}
		else
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Invalid primary target."));
		}
	}

	if (Secondary.Weight > SMALL_NUMBER)
	{
		FVector Target = Secondary.Target;

		if (SecondaryCachedSpace.UpdateCache(Secondary.Space, Hierarchy))
		{
			FTransform Space = Hierarchy->GetGlobalTransform(SecondaryCachedSpace);
			if (Secondary.Kind == EControlRigVectorKind::Direction)
			{
				Target = Space.TransformVectorNoScale(Target);
			}
			else
			{
				Target = Space.TransformPositionNoScale(Target);
			}
		}

		if (Context.DrawInterface != nullptr && DebugSettings.bEnabled)
		{
			const FLinearColor Color = FLinearColor(0.f, 0.2f, 1.f, 1.f);
			if (Secondary.Kind == EControlRigVectorKind::Direction)
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Result.GetLocation(), Result.GetLocation() + Target * DebugSettings.Scale, Color);
			}
			else
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Result.GetLocation(), Target, Color);
				Context.DrawInterface->DrawBox(DebugSettings.WorldOffset, FTransform(FQuat::Identity, Target, FVector(1.f, 1.f, 1.f) * DebugSettings.Scale * 0.1f), Color);
			}
		}

		if (Secondary.Kind == EControlRigVectorKind::Location)
		{
			Target = Target - Result.GetLocation();
		}

		if (!Primary.Axis.IsNearlyZero())
		{
			FVector PrimaryAxis = Result.TransformVectorNoScale(Primary.Axis).GetSafeNormal();
			Target = Target - FVector::DotProduct(Target, PrimaryAxis) * PrimaryAxis;
		}

		if (!Target.IsNearlyZero() && !Secondary.Axis.IsNearlyZero())
		{
			Target = Target.GetSafeNormal();

			FVector Axis = Result.TransformVectorNoScale(Secondary.Axis).GetSafeNormal();
			float T = Secondary.Weight * Weight;
			if (T < 1.f - SMALL_NUMBER)
			{
				Target = FMath::Lerp<FVector>(Axis, Target, T).GetSafeNormal();
			}
			FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
			Result.SetRotation((Rotation * Result.GetRotation()).GetNormalized());
		}
		else
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Invalid secondary target."));
		}
	}
}

FRigUnit_AimBone_Execute()
{
	FRigUnit_AimItem_Target PrimaryTargetItem;
	PrimaryTargetItem.Weight = Primary.Weight;
	PrimaryTargetItem.Axis = Primary.Axis;
	PrimaryTargetItem.Target = Primary.Target;
	PrimaryTargetItem.Kind = Primary.Kind;
	PrimaryTargetItem.Space = FRigElementKey(Primary.Space, ERigElementType::Bone);

	FRigUnit_AimItem_Target SecondaryTargetItem;
	SecondaryTargetItem.Weight = Secondary.Weight;
	SecondaryTargetItem.Axis = Secondary.Axis;
	SecondaryTargetItem.Target = Secondary.Target;
	SecondaryTargetItem.Kind = Secondary.Kind;
	SecondaryTargetItem.Space = FRigElementKey(Secondary.Space, ERigElementType::Bone);

	FRigUnit_AimItem::StaticExecute(
		RigVMExecuteContext,
		FRigElementKey(Bone, ERigElementType::Bone),
		PrimaryTargetItem,
		SecondaryTargetItem,
		Weight,
		DebugSettings,
		CachedBoneIndex,
		PrimaryCachedSpace,
		SecondaryCachedSpace,
		ExecuteContext,
		Context);
}

FRigUnit_AimItem_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigHierarchyContainer* Hierarchy = ExecuteContext.Hierarchy;
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		CachedItem.Reset();
		PrimaryCachedSpace.Reset();
		SecondaryCachedSpace.Reset();
		return;
	}

	if (!CachedItem.UpdateCache(Item, Hierarchy))
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Item not found '%s'."), *Item.ToString());
		return;
	}

	if ((Weight <= SMALL_NUMBER) || (Primary.Weight <= SMALL_NUMBER && Secondary.Weight <= SMALL_NUMBER))
	{
		return;
	}

	FTransform Transform = Hierarchy->GetGlobalTransform(CachedItem);

	FRigUnit_AimBoneMath::StaticExecute(
		RigVMExecuteContext,
		Transform,
		Primary,
		Secondary,
		Weight,
		Transform,
		DebugSettings,
		PrimaryCachedSpace,
		SecondaryCachedSpace,
		Context);

	Hierarchy->SetGlobalTransform(CachedItem, Transform);
}