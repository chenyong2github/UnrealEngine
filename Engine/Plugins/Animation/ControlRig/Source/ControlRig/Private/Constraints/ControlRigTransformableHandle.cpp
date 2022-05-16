// Copyright Epic Games, Inc. All Rights Reserved.


#include "Constraints/ControlRigTransformableHandle.h"

#include "ControlRig.h"
#include "IControlRigObjectBinding.h"
#include "Rigs/RigHierarchyElements.h"

/**
 * UTransformableControlHandle
 */

UTransformableControlHandle::~UTransformableControlHandle()
{}

bool UTransformableControlHandle::IsValid() const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return false;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return false;
	}

	const FRigControlElement* ControlElement = ControlRig->FindControl(ControlName);
	if (!ControlElement)
	{
		return false;
	}
	
	return true;
}

// NOTE should we cache the skeletal mesh and the CtrlIndex to avoid looking for if every time
// probably not for handling runtime changes
void UTransformableControlHandle::SetTransform(const FTransform& InGlobal) const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return;
	}

	const FRigControlElement* ControlElement = ControlRig->FindControl(ControlName);
	if (!ControlElement)
	{
		return;
	}
	
	URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);
	
	const FTransform& ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
	Hierarchy->SetGlobalTransform(CtrlIndex, InGlobal.GetRelativeTransform(ComponentTransform));
}

// NOTE should we cache the skeletal mesh and the CtrlIndex to avoid looking for if every time
// probably not for handling runtime changes
FTransform UTransformableControlHandle::GetTransform() const
{
	if (!ControlRig.IsValid() || ControlName == NAME_None)
	{
		return FTransform::Identity;
	}

	const USkeletalMeshComponent* SkeletalMeshComponent = GetSkeletalMesh();
	if (!SkeletalMeshComponent)
	{
		return FTransform::Identity;
	}

	const FRigControlElement* ControlElement = ControlRig->FindControl(ControlName);
	if (!ControlElement)
	{
		return FTransform::Identity;
	}
	
	const FRigElementKey& ControlKey = ControlElement->GetKey();
	const URigHierarchy* Hierarchy = ControlRig->GetHierarchy();
	const int32 CtrlIndex = Hierarchy->GetIndex(ControlKey);

	const FTransform& ComponentTransform = SkeletalMeshComponent->GetComponentTransform();
	return Hierarchy->GetGlobalTransform(CtrlIndex) * ComponentTransform;
}

UObject* UTransformableControlHandle::GetPrerequisiteObject() const
{
	return GetSkeletalMesh(); 
}

FTickFunction* UTransformableControlHandle::GetTickFunction() const
{
	USkeletalMeshComponent* SkelMeshComponent = GetSkeletalMesh();
	return SkelMeshComponent ? &SkelMeshComponent->PrimaryComponentTick : nullptr;
}

uint32 UTransformableControlHandle::GetHash() const
{
	if (ControlRig.IsValid() && ControlName != NAME_None)
	{
		return HashCombine(GetTypeHash(ControlRig.Get()), GetTypeHash(ControlName));
	}
	return 0;
}

USkeletalMeshComponent* UTransformableControlHandle::GetSkeletalMesh() const
{
	const TSharedPtr<IControlRigObjectBinding> ObjectBinding = ControlRig.IsValid() ? ControlRig->GetObjectBinding() : nullptr;
	return ObjectBinding ? Cast<USkeletalMeshComponent>(ObjectBinding->GetBoundObject()) : nullptr;
}

#if WITH_EDITOR
FName UTransformableControlHandle::GetName() const
{
	const USkeletalMeshComponent* SkeletalMesh = GetSkeletalMesh();
	
	const AActor* Actor = SkeletalMesh ? SkeletalMesh->GetOwner() : nullptr;
	const FName ControlRigName = Actor ? FName(*Actor->GetActorLabel()) : SkeletalMesh ? SkeletalMesh->GetFName() : NAME_None; 

	const FString FullName = FString::Printf(TEXT("%s/%s"), *ControlRigName.ToString(), *ControlName.ToString() );

	return FName(*FullName);
}
#endif