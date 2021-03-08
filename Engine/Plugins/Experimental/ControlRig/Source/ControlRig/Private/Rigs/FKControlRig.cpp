// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/FKControlRig.h"
#include "Animation/SmartName.h"
#include "Engine/SkeletalMesh.h"
#include "IControlRigObjectBinding.h"
#include "Components/SkeletalMeshComponent.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Units/Execution/RigUnit_InverseExecution.h"

#define LOCTEXT_NAMESPACE "OverrideControlRig"

UFKControlRig::UFKControlRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ApplyMode(EControlRigFKRigExecuteMode::Replace)
{
	bResetInitialTransformsBeforeSetup = false;
}

FName UFKControlRig::GetControlName(const FName& InBoneName)
{
	if (InBoneName != NAME_None)
	{
		return FName(*(InBoneName.ToString() + TEXT("_CONTROL")));
	}

	// if bone name is coming as none, we don't append
	return NAME_None;
}

FName UFKControlRig::GetSpaceName(const FName& InBoneName)
{
	if (InBoneName != NAME_None)
	{
		return FName(*(InBoneName.ToString() + TEXT("_SPACE")));
	}

	// if bone name is coming as none, we don't append
	return NAME_None;
}

void UFKControlRig::ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName)
{
	if (InOutContext.State != EControlRigState::Update)
	{
		return;
	}

	if (InEventName == FRigUnit_BeginExecution::EventName)
	{
		FRigVMExecuteContext VMContext;

		FRigBoneHierarchy& BoneHierarchy = GetBoneHierarchy();
		FRigCurveContainer& CurveContainer = GetCurveContainer();
		const FRigControlHierarchy& ControlHierarchy = GetControlHierarchy();

		for(FRigBone& Bone : BoneHierarchy)
		{
			FName ControlName = GetControlName(Bone.Name);
			const int32 ControlIndex = ControlHierarchy.GetIndex(ControlName);
			if (IsControlActive[ControlIndex])
			{
				FTransform Transform = ControlHierarchy[ControlIndex].GetValue(ERigControlValueType::Current).Get<FEulerTransform>().ToFTransform();
				switch (ApplyMode)
				{
					case EControlRigFKRigExecuteMode::Replace:
					{
						BoneHierarchy.SetLocalTransform(Bone.Index, Transform, false);
						break;
					}
					case EControlRigFKRigExecuteMode::Additive:
					{
						FTransform PreviousTransform = BoneHierarchy.GetLocalTransform(Bone.Index);
						BoneHierarchy.SetLocalTransform(Bone.Index, Transform * PreviousTransform, false);
						break;
					}
				}
			}
		}

		BoneHierarchy.RecomputeGlobalTransforms();

		for (FRigCurve& Curve : CurveContainer)
		{
			FName ControlName = GetControlName(Curve.Name);
			const int32 ControlIndex = ControlHierarchy.GetIndex(ControlName);
			if (IsControlActive[ControlIndex])
			{
				float CurveValue = ControlHierarchy[ControlIndex].GetValue(ERigControlValueType::Current).Get<float>();
				switch (ApplyMode)
				{
				case EControlRigFKRigExecuteMode::Replace:
				{
					CurveContainer.SetValue(Curve.Index, CurveValue);
					break;
				}
				case EControlRigFKRigExecuteMode::Additive:
				{
					float PreviousValue = CurveContainer.GetValue(Curve.Index);
					CurveContainer.SetValue(Curve.Index, PreviousValue + CurveValue);
					break;
				}
				}
			}
		}
	}
	else if (InEventName == FRigUnit_InverseExecution::EventName)
	{
		FRigVMExecuteContext VMContext;

		const FRigBoneHierarchy& BoneHierarchy = GetBoneHierarchy();
		const FRigCurveContainer& CurveContainer = GetCurveContainer();
		FRigControlHierarchy& ControlHierarchy = GetControlHierarchy();

		for (const FRigBone& Bone : BoneHierarchy)
		{
			FName ControlName = GetControlName(Bone.Name);
			const int32 ControlIndex = ControlHierarchy.GetIndex(ControlName);
			if (IsControlActive[ControlIndex])
			{
				FEulerTransform EulerTransform(BoneHierarchy.GetLocalTransform(Bone.Index));
				SetControlValue<FEulerTransform>(ControlName, EulerTransform);
			}
		}

		for (const FRigCurve& Curve : CurveContainer)
		{
			FName ControlName = GetControlName(Curve.Name);
			const int32 ControlIndex = ControlHierarchy.GetIndex(ControlName);
			if (IsControlActive[ControlIndex])
			{
				SetControlValue<float>(ControlName, CurveContainer.GetValue(Curve.Index));
			}
		}
	}
}

void UFKControlRig::Initialize(bool bInitRigUnits /*= true*/)
{
	Super::Initialize(bInitRigUnits);

	if (GetObjectBinding() == nullptr)
	{
		return;
	}

	// we do this after Initialize because Initialize will copy from CDO. 
	// create hierarchy from the incoming skeleton
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(GetObjectBinding()->GetBoundObject()))
	{
		CreateRigElements(SkeletalMeshComponent->SkeletalMesh);
	}

	// execute init
	Execute(EControlRigState::Init, FRigUnit_BeginExecution::EventName);
}

TArray<FName> UFKControlRig::GetControlNames()
{
	TArray<FRigControl> Controls;
	GetControlsInOrder(Controls);

	TArray<FName> Names;
	for (FRigControl& Control: Controls)
	{
		Names.Add(Control.Name);
	}
	return Names;
}

bool UFKControlRig::GetControlActive(int32 Index) const
{
	if (Index >= 0 && Index < IsControlActive.Num())
	{
		return IsControlActive[Index];
	}
	return false;
}


void UFKControlRig::SetControlActive(int32 Index, bool bActive)
{
	if (Index >= 0 && Index < IsControlActive.Num())
	{
		IsControlActive[Index] = bActive;
	}
}

void UFKControlRig::SetControlActive(const TArray<FFKBoneCheckInfo>& BoneChecks)
{
	for (const FFKBoneCheckInfo& Info : BoneChecks)
	{
		SetControlActive(Info.BoneID, Info.bActive);
	}
}

void UFKControlRig::CreateRigElements(const FReferenceSkeleton& InReferenceSkeleton, const FSmartNameMapping* InSmartNameMapping)
{
	FRigHierarchyContainer* Container = GetHierarchy();
	Container->Reset();
	FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	BoneHierarchy.ImportSkeleton(InReferenceSkeleton, NAME_None, false, false, true, false);
	FRigCurveContainer& CurveContainer = Container->CurveContainer;

	if (InSmartNameMapping)
	{
		TArray<FName> NameArray;
		InSmartNameMapping->FillNameArray(NameArray);
		for (int32 Index = 0; Index < NameArray.Num(); ++Index)
		{
			CurveContainer.Add(NameArray[Index]);
		}
	}

	if (IsControlActive.Num() != (BoneHierarchy.Num() + CurveContainer.Num()))
	{
		IsControlActive.SetNum(BoneHierarchy.Num() + CurveContainer.Num());
		for (bool& bIsActive : IsControlActive)
		{
			bIsActive = true;
		}
	}

	// add control for all bone hierarchy 
	int32 ControlIndex = 0;
	for (; ControlIndex < BoneHierarchy.Num(); ++ControlIndex)
	{
		const FRigBone& RigBone = BoneHierarchy[ControlIndex];
		FName BoneName = RigBone.Name;
		FName ParentName = RigBone.ParentName;
		FName SpaceName = GetSpaceName(BoneName);// name conflict?
		FName ControlName = GetControlName(BoneName); // name conflict?
		FTransform LocalTransform = FTransform::Identity;
		if (ParentName != NAME_None)
		{
			FTransform Transform = BoneHierarchy.GetGlobalTransform(BoneName);
			FTransform ParentTransform = BoneHierarchy.GetGlobalTransform(ParentName);
			LocalTransform = Transform.GetRelativeTransform(ParentTransform);
			Container->SpaceHierarchy.Add(SpaceName, ERigSpaceType::Bone, ParentName);
		}
		else
		{
			FTransform Transform = BoneHierarchy.GetGlobalTransform(BoneName);
			FTransform ParentTransform = FTransform::Identity;
			LocalTransform = Transform.GetRelativeTransform(ParentTransform);
			Container->SpaceHierarchy.Add(SpaceName, ERigSpaceType::Global, ParentName);
		}

		FRigControl& RigControl = Container->ControlHierarchy.Add(ControlName, ERigControlType::EulerTransform, NAME_None, SpaceName); // NAME_None, LocalTransform);//SpaceName);  LocalTransform);
		RigControl.DisplayName = BoneName;
		RigControl.SetValueFromTransform(LocalTransform, ERigControlValueType::Initial);
	}
	for (int32 Index = 0; Index < CurveContainer.Num(); ++Index)
	{
		FRigCurve& RigCurve = CurveContainer[Index];
		FName ControlName = GetControlName(RigCurve.Name); // name conflict?

		FRigControl& RigControl = Container->ControlHierarchy.Add(ControlName, ERigControlType::Float, NAME_None, NAME_None); // NAME_None, LocalTransform);//SpaceName);  LocalTransform);
		RigControl.DisplayName = RigCurve.Name;
		RigControl.Value.Set<float>(RigCurve.Value);
	}

	Container->Initialize(true);
}

void UFKControlRig::CreateRigElements(const USkeletalMesh* InReferenceMesh)
{
	if (InReferenceMesh)
	{
		const USkeleton* Skeleton = InReferenceMesh->GetSkeleton();
		CreateRigElements(InReferenceMesh->GetRefSkeleton(), (Skeleton) ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr);
	}
}

void UFKControlRig::ToggleApplyMode()
{
	if (ApplyMode == EControlRigFKRigExecuteMode::Additive)
	{
		ApplyMode = EControlRigFKRigExecuteMode::Replace;
	}
	else
	{
		ApplyMode = EControlRigFKRigExecuteMode::Additive;
	}

	if (ApplyMode == EControlRigFKRigExecuteMode::Additive)
	{
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Never;

		FRigControlHierarchy& ControlHierarchy = GetControlHierarchy();
		FTransform ZeroScale = FTransform::Identity;
		ZeroScale.SetScale3D(FVector::ZeroVector);
		FEulerTransform EulerZero(ZeroScale);
		for (int32 ControlIndex = 0; ControlIndex < ControlHierarchy.Num(); ControlIndex++)
		{
			const FRigControl& ControlToChange = ControlHierarchy[ControlIndex];
			if (ControlToChange.ControlType == ERigControlType::EulerTransform)
			{
				SetControlValue<FEulerTransform>(ControlToChange.Name, EulerZero, true, Context);
			}
			else if (ControlToChange.ControlType == ERigControlType::Float)
			{
				SetControlValue<float>(ControlToChange.Name, 0.f, true, Context);
			}
		}
	}
	else
	{
		FRigControlHierarchy& ControlHierarchy = GetControlHierarchy();
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Never;

		for (int32 ControlIndex = 0; ControlIndex < ControlHierarchy.Num(); ControlIndex++)
		{
			const FRigControl& ControlToChange = ControlHierarchy[ControlIndex];
			if (ControlToChange.ControlType == ERigControlType::EulerTransform)
			{
				const FRigControl* Control = FindControl(ControlToChange.Name);
				if (Control)
				{
					FEulerTransform InitValue = Control->InitialValue.Get<FEulerTransform>();
					SetControlValue<FEulerTransform>(ControlToChange.Name, InitValue, true, Context);
				}
			}
			else if (ControlToChange.ControlType == ERigControlType::Float)
			{
				const FRigControl* Control = FindControl(ControlToChange.Name);
				if (Control)
				{
					float InitValue = Control->InitialValue.Get<float>();
					SetControlValue<float>(ControlToChange.Name, InitValue, true, Context);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE


