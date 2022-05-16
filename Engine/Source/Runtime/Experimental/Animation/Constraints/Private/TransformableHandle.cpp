// Copyright Epic Games, Inc. All Rights Reserved.


#include "TransformableHandle.h"

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

void UTransformableComponentHandle::PostLoad()
{
	Super::PostLoad();
}

bool UTransformableComponentHandle::IsValid() const
{
	return Component.IsValid();
}

void UTransformableComponentHandle::SetTransform(const FTransform& InGlobal) const
{
	if(Component.IsValid())
	{
		Component->SetWorldTransform(InGlobal);
	}
}

FTransform UTransformableComponentHandle::GetTransform() const
{
	return Component.IsValid() ? Component->GetComponentTransform() : FTransform::Identity;
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