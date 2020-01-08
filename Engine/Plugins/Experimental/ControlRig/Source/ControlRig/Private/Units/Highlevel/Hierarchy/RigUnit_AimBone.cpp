// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Highlevel/Hierarchy/RigUnit_AimBone.h"
#include "Units/RigUnitContext.h"

FRigUnit_AimBone_Execute()
{
    DECLARE_SCOPE_HIERARCHICAL_COUNTER_RIGUNIT()
	FRigBoneHierarchy* Hierarchy = ExecuteContext.GetBones();
	if (Hierarchy == nullptr)
	{
		return;
	}

	if (Context.State == EControlRigState::Init)
	{
		BoneIndex = Hierarchy->GetIndex(Bone);
		return;
	}

	if (BoneIndex == INDEX_NONE)
	{
		UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Bone not found '%s'."), *Bone.ToString());
		return;
	}

	if (Primary.Weight <= SMALL_NUMBER && Secondary.Weight <= SMALL_NUMBER)
	{
		return;
	}
	FTransform Transform = Hierarchy->GetGlobalTransform(BoneIndex);

	if (Primary.Weight > SMALL_NUMBER)
	{
		FVector Target = Primary.Target;

		if (PrimaryCachedSpaceName != Primary.Space || PrimaryCachedSpaceIndex == INDEX_NONE)
		{
			if (Primary.Space == NAME_None)
			{
				PrimaryCachedSpaceIndex = INDEX_NONE;
			}
			else
			{
				PrimaryCachedSpaceIndex = Hierarchy->GetIndex(Primary.Space);
			}
			PrimaryCachedSpaceName = Primary.Space;
		}

		if (PrimaryCachedSpaceIndex != INDEX_NONE)
		{
			FTransform Space = Hierarchy->GetGlobalTransform(PrimaryCachedSpaceIndex);
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
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Transform.GetLocation(), Transform.GetLocation() + Target * DebugSettings.Scale, Color);
			}
			else
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Transform.GetLocation(), Target, Color);
				Context.DrawInterface->DrawBox(DebugSettings.WorldOffset, FTransform(FQuat::Identity, Target, FVector(1.f, 1.f, 1.f) * DebugSettings.Scale * 0.1f), Color);
			}
		}

		if (Primary.Kind == EControlRigVectorKind::Location)
		{
			Target = Target - Transform.GetLocation();
		}

		if (!Target.IsNearlyZero() && !Primary.Axis.IsNearlyZero())
		{
			Target = Target.GetSafeNormal();
			FVector Axis = Transform.TransformVectorNoScale(Primary.Axis).GetSafeNormal();
			if (Primary.Weight < 1.f - SMALL_NUMBER)
			{
				Target = FMath::Lerp<FVector>(Axis, Target, Primary.Weight).GetSafeNormal();
			}
			FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
			Transform.SetRotation((Rotation * Transform.GetRotation()).GetNormalized());
		}
		else
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Invalid primary target."));
		}
	}

	if (Secondary.Weight > SMALL_NUMBER)
	{
		FVector Target = Secondary.Target;

		if (SecondaryCachedSpaceName != Secondary.Space || SecondaryCachedSpaceIndex == INDEX_NONE)
		{
			if (Secondary.Space == NAME_None)
			{
				SecondaryCachedSpaceIndex = INDEX_NONE;
			}
			else
			{
				SecondaryCachedSpaceIndex = Hierarchy->GetIndex(Secondary.Space);
			}
			SecondaryCachedSpaceName = Secondary.Space;
		}

		if (SecondaryCachedSpaceIndex != INDEX_NONE)
		{
			FTransform Space = Hierarchy->GetGlobalTransform(SecondaryCachedSpaceIndex);
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
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Transform.GetLocation(), Transform.GetLocation() + Target * DebugSettings.Scale, Color);
			}
			else
			{
				Context.DrawInterface->DrawLine(DebugSettings.WorldOffset, Transform.GetLocation(), Target, Color);
				Context.DrawInterface->DrawBox(DebugSettings.WorldOffset, FTransform(FQuat::Identity, Target, FVector(1.f, 1.f, 1.f) * DebugSettings.Scale * 0.1f), Color);
			}
		}

		if (Secondary.Kind == EControlRigVectorKind::Location)
		{
			Target = Target - Transform.GetLocation();
		}

		if (!Primary.Axis.IsNearlyZero())
		{
			FVector PrimaryAxis = Transform.TransformVectorNoScale(Primary.Axis).GetSafeNormal();
			Target = Target - FVector::DotProduct(Target, PrimaryAxis) * PrimaryAxis;
		}

		if (!Target.IsNearlyZero() && !Secondary.Axis.IsNearlyZero())
		{
			Target = Target.GetSafeNormal();

			FVector Axis = Transform.TransformVectorNoScale(Secondary.Axis).GetSafeNormal();
			if (Secondary.Weight < 1.f - SMALL_NUMBER)
			{
				Target = FMath::Lerp<FVector>(Axis, Target, Secondary.Weight).GetSafeNormal();
			}
			FQuat Rotation = FQuat::FindBetweenNormals(Axis, Target);
			Transform.SetRotation((Rotation * Transform.GetRotation()).GetNormalized());
		}
		else
		{
			UE_CONTROLRIG_RIGUNIT_REPORT_WARNING(TEXT("Invalid secondary target."));
		}
	}

	Hierarchy->SetGlobalTransform(BoneIndex, Transform, bPropagateToChildren);
}