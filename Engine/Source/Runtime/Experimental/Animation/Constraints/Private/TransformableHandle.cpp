// Copyright Epic Games, Inc. All Rights Reserved.


#include "TransformableHandle.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

/**
 * UTransformableHandle
 */

UTransformableHandle::~UTransformableHandle()
{}

void UTransformableHandle::PostLoad()
{
	Super::PostLoad();
}

/**
 * UTransformableComponentHandle
 */

UTransformableComponentHandle::~UTransformableComponentHandle()
{}

bool UTransformableComponentHandle::IsValid() const
{
	return Component.IsValid();
}

void UTransformableComponentHandle::SetGlobalTransform(const FTransform& InGlobal) const
{
	if(Component.IsValid())
	{
		Component->SetWorldTransform(InGlobal);
	}
}

void UTransformableComponentHandle::SetLocalTransform(const FTransform& InLocal) const
{
	if(Component.IsValid())
	{
		Component->SetRelativeTransform(InLocal);
	}
}

FTransform UTransformableComponentHandle::GetLocalTransform() const
{
	return Component.IsValid() ? Component->GetRelativeTransform() : FTransform::Identity;
}

FTransform UTransformableComponentHandle::GetGlobalTransform() const
{
	return Component.IsValid() ? Component->GetComponentTransform() : FTransform::Identity;
}

FTransform UTransformableComponentHandle::GetParentTransform() const
{
	return (Component.IsValid() && Component->GetAttachParent()) ? Component->GetAttachParent()->GetComponentTransform() : FTransform::Identity;
}

UObject* UTransformableComponentHandle::GetPrerequisiteObject() const
{
	return Component.Get(); 
}

FTickFunction* UTransformableComponentHandle::GetTickFunction() const
{
	return Component.IsValid() ? &Component->PrimaryComponentTick : nullptr;
}

uint32 UTransformableComponentHandle::GetHash() const
{
	return Component.IsValid() ? GetTypeHash(Component.Get()) : 0;
}

TWeakObjectPtr<UObject> UTransformableComponentHandle::GetTarget() const
{
	return Component;
}

#if WITH_EDITOR
FName UTransformableComponentHandle::GetName() const
{
	if (!Component.IsValid())
	{
		return NAME_None;
	}

	const AActor* Actor = Component->GetOwner();
	return Actor ? FName(*Actor->GetActorLabel()) : Component->GetFName();
}
#endif