// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rigs/AdditiveControlRig.h"
#include "Animation/SmartName.h"
#include "Engine/SkeletalMesh.h"
#include "IControlRigObjectBinding.h"
#include "Components/SkeletalMeshComponent.h"
#include "Units/Execution/RigUnit_BeginExecution.h"

#define LOCTEXT_NAMESPACE "AdditiveControlRig"

UAdditiveControlRig::UAdditiveControlRig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bResetInitialTransformsBeforeSetup = false;
}

FName UAdditiveControlRig::GetControlName(const FName& InBoneName)
{
	if (InBoneName != NAME_None)
	{
		return FName(*(InBoneName.ToString() + TEXT("_CONTROL")));
	}

	// if bone name is coming as none, we don't append
	return NAME_None;
}

FName UAdditiveControlRig::GetSpaceName(const FName& InBoneName)
{
	if (InBoneName != NAME_None)
	{
		return FName(*(InBoneName.ToString() + TEXT("_SPACE")));
	}

	// if bone name is coming as none, we don't append
	return NAME_None;
}

void UAdditiveControlRig::ExecuteUnits(FRigUnitContext& InOutContext, const FName& InEventName)
{
	if (InEventName != FRigUnit_BeginExecution::EventName)
	{
		return;
	}

	FRigControlHierarchy& ControlHierarchy = GetControlHierarchy();
	for (FRigUnit_AddBoneTransform& Unit : AddBoneRigUnits)
	{
		FName ControlName = GetControlName(Unit.Bone);
		const int32 Index = ControlHierarchy.GetIndex(ControlName);
		Unit.Transform = ControlHierarchy.GetLocalTransform(Index);
		Unit.ExecuteContext.Hierarchy = GetHierarchy();
		Unit.ExecuteContext.EventName = InEventName;
		Unit.Execute(InOutContext);
	}
}

void UAdditiveControlRig::Initialize(bool bInitRigUnits /*= true*/)
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

	// add units and initialize
	AddBoneRigUnits.Reset();
	FRigBoneHierarchy& BoneHierarchy = GetBoneHierarchy();

	for (int32 BoneIndex = 0; BoneIndex < BoneHierarchy.Num(); ++BoneIndex)
	{
		FRigUnit_AddBoneTransform NewUnit;
		NewUnit.Bone = BoneHierarchy[BoneIndex].Name;
		NewUnit.bPropagateToChildren = true;
		AddBoneRigUnits.Add(NewUnit);
	}

	// execute init
	Execute(EControlRigState::Init, FRigUnit_BeginExecution::EventName);
}

void UAdditiveControlRig::CreateRigElements(const FReferenceSkeleton& InReferenceSkeleton, const FSmartNameMapping* InSmartNameMapping)
{
	FRigHierarchyContainer* Container = GetHierarchy();
	Container->Reset();
	FRigBoneHierarchy& BoneHierarchy = Container->BoneHierarchy;
	BoneHierarchy.ImportSkeleton(InReferenceSkeleton, NAME_None, false, false, true, false);

	if (InSmartNameMapping)
	{
		FRigCurveContainer& CurveContainer = Container->CurveContainer;
		TArray<FName> NameArray;
		InSmartNameMapping->FillNameArray(NameArray);
		for (int32 Index = 0; Index < NameArray.Num(); ++Index)
		{
			CurveContainer.Add(NameArray[Index]);
		}
	}

	// add control for all bone hierarchy 
	for (int32 BoneIndex = 0; BoneIndex < BoneHierarchy.Num(); ++BoneIndex)
	{

		const FRigBone& RigBone = BoneHierarchy[BoneIndex];
		FName BoneName = RigBone.Name;
		FName ParentName = RigBone.ParentName;
		FName SpaceName = GetSpaceName(BoneName);// name conflict?
		FName ControlName = GetControlName(BoneName); // name conflict?
		if (ParentName != NAME_None)
		{
			FTransform Transform = BoneHierarchy.GetGlobalTransform(BoneName);
			FTransform ParentTransform = BoneHierarchy.GetGlobalTransform(ParentName);
			FTransform LocalTransform = Transform.GetRelativeTransform(ParentTransform);
			FRigSpace& Space = Container->SpaceHierarchy.Add(SpaceName, ERigSpaceType::Bone, ParentName);
			Space.InitialTransform = LocalTransform;
		}
		else
		{
			FTransform Transform = BoneHierarchy.GetGlobalTransform(BoneName);
			FTransform ParentTransform = FTransform::Identity;
			FTransform LocalTransform = Transform.GetRelativeTransform(ParentTransform);
			FRigSpace& Space = Container->SpaceHierarchy.Add(SpaceName, ERigSpaceType::Global, ParentName);
			Space.InitialTransform = LocalTransform;
		}

		FRigControl& RigControl = Container->ControlHierarchy.Add(ControlName, ERigControlType::Transform, NAME_None, SpaceName);
		RigControl.DisplayName = BoneName;
	}

	Container->Initialize(true);
}

void UAdditiveControlRig::CreateRigElements(const USkeletalMesh* InReferenceMesh)
{
	if (InReferenceMesh)
	{
		const USkeleton* Skeleton = InReferenceMesh->GetSkeleton();
		CreateRigElements(InReferenceMesh->GetRefSkeleton(), (Skeleton) ? Skeleton->GetSmartNameContainer(USkeleton::AnimCurveMappingName) : nullptr);
	}
}

#undef LOCTEXT_NAMESPACE


