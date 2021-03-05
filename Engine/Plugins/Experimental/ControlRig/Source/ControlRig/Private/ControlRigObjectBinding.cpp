// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigObjectBinding.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "ControlRigComponent.h"

FControlRigObjectBinding::~FControlRigObjectBinding()
{
}

void FControlRigObjectBinding::BindToObject(UObject* InObject)
{
	// If we are binding to an actor, find the first skeletal mesh component
	if (AActor* Actor = Cast<AActor>(InObject))
	{
		if (UControlRigComponent* ControlRigComponent = Actor->FindComponentByClass<UControlRigComponent>())
		{
			SceneComponent = ControlRigComponent;
		}
		else if (USkeletalMeshComponent* SkeletalMeshComponent = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			SceneComponent = SkeletalMeshComponent;
		}
	}
	else if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(InObject))
	{
		SceneComponent = ControlRigComponent;
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InObject))
	{
		SceneComponent = SkeletalMeshComponent;
	}

	ControlRigBind.Broadcast(SceneComponent.Get());
}

void FControlRigObjectBinding::UnbindFromObject()
{
	SceneComponent = nullptr;

	ControlRigUnbind.Broadcast();
}

bool FControlRigObjectBinding::IsBoundToObject(UObject* InObject) const
{
	if (AActor* Actor = Cast<AActor>(InObject))
	{
		if (UControlRigComponent* ControlRigComponent = Actor->FindComponentByClass<UControlRigComponent>())
		{
			return SceneComponent.Get() == ControlRigComponent;
		}
		else if (USkeletalMeshComponent* SkeletalMeshComponent = Actor->FindComponentByClass<USkeletalMeshComponent>())
		{
			return SceneComponent.Get() == SkeletalMeshComponent;
		}
	}
	else if (UControlRigComponent* ControlRigComponent = Cast<UControlRigComponent>(InObject))
	{
		return SceneComponent.Get() == ControlRigComponent;
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(InObject))
	{
		return SceneComponent.Get() == SkeletalMeshComponent;
	}

	return false;
}

UObject* FControlRigObjectBinding::GetBoundObject() const
{
	return SceneComponent.Get();
}

AActor* FControlRigObjectBinding::GetHostingActor() const
{
	if (SceneComponent.Get())
	{
		return SceneComponent->GetOwner();
	}

	return nullptr;
}
