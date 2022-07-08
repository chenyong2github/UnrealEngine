// Copyright Epic Games, Inc. All Rights Reserved.


#include "TransformableHandle.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/Engine.h"

/**
 * UTransformableHandle
 */

UTransformableHandle::~UTransformableHandle()
{
	OnHandleModified.Clear();
}

void UTransformableHandle::PostLoad()
{
	Super::PostLoad();
}

UTransformableHandle::FHandleModifiedEvent& UTransformableHandle::HandleModified()
{
	return OnHandleModified;
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

void UTransformableComponentHandle::UnregisterDelegates() const
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	GEngine->OnActorMoving().RemoveAll(this);
#endif
}

void UTransformableComponentHandle::RegisterDelegates()
{
	UnregisterDelegates();

#if WITH_EDITOR
	GEngine->OnActorMoving().AddUObject(this, &UTransformableComponentHandle::OnActorMoving);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UTransformableComponentHandle::OnPostPropertyChanged);
#endif
}

void UTransformableComponentHandle::OnActorMoving(AActor* InActor)
{
	if (!Component.IsValid())
	{
		return;
	}

	const USceneComponent* SceneComponent = InActor ? InActor->GetRootComponent() : nullptr;
	if (SceneComponent != Component)
	{
		return;
	}

	if(OnHandleModified.IsBound())
	{
		OnHandleModified.Broadcast(this, true);
	}
}

void UTransformableComponentHandle::OnPostPropertyChanged(
	UObject* InObject,
	FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (!Component.IsValid())
	{
		return;
	}

	USceneComponent* SceneComponent = Cast<USceneComponent>(InObject);
	if (!SceneComponent)
	{
		if (const AActor* Actor = Cast<AActor>(InObject))
		{
			SceneComponent = Actor->GetRootComponent();
		}
	}
	
	if (SceneComponent!= Component)
	{
		return;
	}

	const FProperty* MemberProperty = InPropertyChangedEvent.MemberProperty;
	if (!MemberProperty)
	{
		return;
	}
	
	const FName MemberPropertyName = MemberProperty->GetFName();
	const bool bTransformationChanged =
		(MemberPropertyName == USceneComponent::GetRelativeLocationPropertyName() ||
			MemberPropertyName == USceneComponent::GetRelativeRotationPropertyName() ||
			MemberPropertyName == USceneComponent::GetRelativeScale3DPropertyName());
	if (!bTransformationChanged)
	{
		return;
	}

	if(OnHandleModified.IsBound())
	{
		OnHandleModified.Broadcast(this, true);
	}
}

#if WITH_EDITOR
FString UTransformableComponentHandle::GetLabel() const
{
	if (!Component.IsValid())
	{
		static const FString DummyLabel;
		return DummyLabel;
	}

	const AActor* Actor = Component->GetOwner();
	return Actor ? Actor->GetActorLabel() : Component->GetName();
}

FString UTransformableComponentHandle::GetFullLabel() const
{
	return GetLabel();
};

#endif