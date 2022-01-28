// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerComponent.h"
#include "MLDeformer.h"
#include "MLDeformerAsset.h"
#include "MLDeformerInstance.h"
#include "Components/SkeletalMeshComponent.h"

UMLDeformerComponent::UMLDeformerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickInEditor = true;
	bAutoActivate = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;
}

void UMLDeformerComponent::SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent)
{
	RemoveNeuralNetworkModifyDelegate();

	DeformerAsset = InDeformerAsset;
	SkelMeshComponent = InSkelMeshComponent;
	AddTickPrerequisiteComponent(SkelMeshComponent);
	DeformerInstance.Init(InDeformerAsset, InSkelMeshComponent);

	AddNeuralNetworkModifyDelegate();
}

void UMLDeformerComponent::AddNeuralNetworkModifyDelegate()
{
	if (DeformerAsset != nullptr)
	{
		NeuralNetworkModifyDelegateHandle = DeformerAsset->NeuralNetworkModifyDelegate.AddLambda(([this]()
		{
			DeformerInstance.Release();
			DeformerInstance.Init(DeformerAsset, SkelMeshComponent);
		}));
	}
}

void UMLDeformerComponent::RemoveNeuralNetworkModifyDelegate()
{
	if (DeformerAsset != nullptr && NeuralNetworkModifyDelegateHandle != FDelegateHandle())
	{
		DeformerAsset->NeuralNetworkModifyDelegate.Remove(NeuralNetworkModifyDelegateHandle);
	}
	
	NeuralNetworkModifyDelegateHandle = FDelegateHandle();
}

void UMLDeformerComponent::Activate(bool bReset)
{
	// If we haven't pointed to some skeletal mesh component to use, then try to find one on the actor.
	USkeletalMeshComponent* SkelMeshComponentToUse = SkelMeshComponent;
	if (SkelMeshComponentToUse == nullptr)
	{
		AActor* Actor = Cast<AActor>(GetOuter());
		SkelMeshComponent = Actor->FindComponentByClass<USkeletalMeshComponent>();
	}

	SetupComponent(DeformerAsset, SkelMeshComponent);
}

void UMLDeformerComponent::Deactivate()
{
	RemoveNeuralNetworkModifyDelegate();
	DeformerInstance.Release();
}

void UMLDeformerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (TickType != ELevelTick::LEVELTICK_PauseTick)
	{
		// Update the deformer, which runs the inference.
		// Only do this when the LOD level is 0.
		if (SkelMeshComponent && SkelMeshComponent->GetPredictedLODLevel() == 0)
		{
			DeformerInstance.Update();
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
