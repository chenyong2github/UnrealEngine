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

		GetHierarchy()->ForEach<FRigBoneElement>([&](FRigBoneElement* BoneElement) -> bool
        {
			const FName ControlName = GetControlName(BoneElement->GetName());
			const FRigElementKey ControlKey(ControlName, ERigElementType::Control);
			const int32 ControlIndex = GetHierarchy()->GetIndex(ControlKey);
			if (IsControlActive[ControlIndex])
			{
				const FTransform Transform = GetHierarchy()->GetLocalTransform(ControlIndex);
				switch (ApplyMode)
				{
					case EControlRigFKRigExecuteMode::Replace:
					{
						GetHierarchy()->SetTransform(BoneElement, Transform, ERigTransformType::CurrentLocal, true, false);
						break;
					}
					case EControlRigFKRigExecuteMode::Additive:
					{
						const FTransform PreviousTransform = GetHierarchy()->GetTransform(BoneElement, ERigTransformType::CurrentLocal);
						GetHierarchy()->SetTransform(BoneElement, Transform * PreviousTransform, ERigTransformType::CurrentLocal, true, false);
						break;
					}
				}
			}
			return true;
		});

		GetHierarchy()->ForEach<FRigCurveElement>([&](FRigCurveElement* CurveElement) -> bool
        {
			const FName ControlName = GetControlName(CurveElement->GetName());
			const FRigElementKey ControlKey(ControlName, ERigElementType::Control);
			const int32 ControlIndex = GetHierarchy()->GetIndex(ControlKey);
			
			if (IsControlActive[ControlIndex])
			{
				const float CurveValue = GetHierarchy()->GetControlValue(ControlIndex).Get<float>();

				switch (ApplyMode)
				{
					case EControlRigFKRigExecuteMode::Replace:
					{
						GetHierarchy()->SetCurveValue(CurveElement, CurveValue, false);
						break;
					}
					case EControlRigFKRigExecuteMode::Additive:
					{
						const float PreviousValue = GetHierarchy()->GetCurveValue(CurveElement);
						GetHierarchy()->SetCurveValue(CurveElement, PreviousValue + CurveValue, false);
						break;
					}
				}
			}
			return true;
		});
	}
	else if (InEventName == FRigUnit_InverseExecution::EventName)
	{
		FRigVMExecuteContext VMContext;

		GetHierarchy()->ForEach<FRigBoneElement>([&](FRigBoneElement* BoneElement) -> bool
        {
            const FName ControlName = GetControlName(BoneElement->GetName());
			const FRigElementKey ControlKey(ControlName, ERigElementType::Control);
			const int32 ControlIndex = GetHierarchy()->GetIndex(ControlKey);
			
			if (IsControlActive[ControlIndex])
			{
				const FEulerTransform EulerTransform(GetHierarchy()->GetTransform(BoneElement, ERigTransformType::CurrentLocal));
				SetControlValue(ControlName, FRigControlValue::Make(EulerTransform));
			}
			
			return true;
		});

		GetHierarchy()->ForEach<FRigCurveElement>([&](FRigCurveElement* CurveElement) -> bool
        {
            const FName ControlName = GetControlName(CurveElement->GetName());
			const FRigElementKey ControlKey(ControlName, ERigElementType::Control);
			const int32 ControlIndex = GetHierarchy()->GetIndex(ControlKey);

			if (IsControlActive[ControlIndex])
			{
				const float CurveValue = GetHierarchy()->GetCurveValue(CurveElement);
				SetControlValue(ControlName, FRigControlValue::Make(CurveValue));
			}

			return true;
		});
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
	TArray<FName> Names;
	for (FRigBaseElement* Element : *GetHierarchy())
	{
		if(Element->IsTypeOf(ERigElementType::Control))
		{
			Names.Add(Element->GetName());
		}
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
	if(Controller == nullptr)
	{
		Controller = NewObject<URigHierarchyController>(this);
		Controller->SetHierarchy(GetHierarchy());
	}
	
	GetHierarchy()->Reset();
	Controller->ImportBones(InReferenceSkeleton, NAME_None, false, false, true, false);

	if (InSmartNameMapping)
	{
		TArray<FName> NameArray;
		InSmartNameMapping->FillNameArray(NameArray);
		for (int32 Index = 0; Index < NameArray.Num(); ++Index)
		{
			Controller->AddCurve(NameArray[Index], 0.f, false);
		}
	}

	// add control for all bone hierarchy 
	int32 ControlIndex = 0;

	GetHierarchy()->ForEach<FRigBoneElement>([&](FRigBoneElement* BoneElement) -> bool
	{
		const FName BoneName = BoneElement->GetName();
		const int32 ParentIndex = GetHierarchy()->GetFirstParent(BoneElement->GetIndex());
		const FName SpaceName = GetSpaceName(BoneName);// name conflict?
		const FName ControlName = GetControlName(BoneName); // name conflict?

		FRigElementKey SpaceKey;

		FTransform LocalTransform;
		if (ParentIndex != INDEX_NONE)
		{
			FTransform GlobalTransform = GetHierarchy()->GetGlobalTransform(BoneElement->GetIndex());
			FTransform ParentTransform = GetHierarchy()->GetGlobalTransform(ParentIndex);
			LocalTransform = GlobalTransform.GetRelativeTransform(ParentTransform);
			SpaceKey = Controller->AddSpace(SpaceName, GetHierarchy()->GetKey(ParentIndex), FTransform::Identity, false, false);
		}
		else
		{
			LocalTransform = GetHierarchy()->GetLocalTransform(BoneElement->GetIndex());
			SpaceKey = Controller->AddSpace(SpaceName, FRigElementKey(), FTransform::Identity, true, false);
		}

		FRigControlSettings Settings;
		Settings.ControlType = ERigControlType::EulerTransform;
		Settings.DisplayName = BoneName;
		const FRigElementKey ControlKey = Controller->AddControl(ControlName, SpaceKey, Settings, FRigControlValue::Make(FEulerTransform::Identity), FTransform::Identity, FTransform::Identity, false);
		GetHierarchy()->SetLocalTransform(ControlKey, LocalTransform, true, true, false);

		return true;
	});
	
	GetHierarchy()->ForEach<FRigCurveElement>([&](FRigCurveElement* CurveElement) -> bool
    {
        const FName ControlName = GetControlName(CurveElement->GetName()); // name conflict?

		FRigControlSettings Settings;
		Settings.ControlType = ERigControlType::Float;
		Settings.DisplayName = CurveElement->GetName();

		Controller->AddControl(ControlName, FRigElementKey(), Settings, FRigControlValue::Make(CurveElement->Value), FTransform::Identity, FTransform::Identity, false);
		
		return true;
	});

	if (IsControlActive.Num() != GetHierarchy()->Num())
	{
		IsControlActive.SetNum(GetHierarchy()->Num());
		for (bool& bIsActive : IsControlActive)
		{
			bIsActive = true;
		}
	}
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

		FTransform ZeroScale = FTransform::Identity;
		ZeroScale.SetScale3D(FVector::ZeroVector);
		const FEulerTransform EulerZero(ZeroScale);

		GetHierarchy()->ForEach<FRigControlElement>([&](FRigControlElement* ControlElement) -> bool
        {
            if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
			{
				SetControlValue<FEulerTransform>(ControlElement->GetName(), EulerZero, true, Context);
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Float)
			{
				SetControlValue<float>(ControlElement->GetName(), 0.f, true, Context);
			}

			return true;
		});
	}
	else
	{
		FRigControlModifiedContext Context;
		Context.SetKey = EControlRigSetKey::Never;

		GetHierarchy()->ForEach<FRigControlElement>([&](FRigControlElement* ControlElement) -> bool
        {
            if (ControlElement->Settings.ControlType == ERigControlType::EulerTransform)
			{
				const FEulerTransform InitValue = GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Initial).Get<FEulerTransform>();
				SetControlValue<FEulerTransform>(ControlElement->GetName(), InitValue, true, Context);
			}
			else if (ControlElement->Settings.ControlType == ERigControlType::Float)
			{
				const float InitValue = GetHierarchy()->GetControlValue(ControlElement, ERigControlValueType::Initial).Get<float>();
				SetControlValue<float>(ControlElement->GetName(), InitValue, true, Context);
			}

			return true;
		});
	}
}

#undef LOCTEXT_NAMESPACE


