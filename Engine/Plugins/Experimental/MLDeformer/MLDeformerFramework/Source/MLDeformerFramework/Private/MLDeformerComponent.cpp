// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerComponent.h"
#include "MLDeformerAsset.h"
#include "MLDeformerModelInstance.h"
#include "MLDeformerModel.h"
#include "Components/SkeletalMeshComponent.h"

UMLDeformerComponent::UMLDeformerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bTickInEditor = true;
	bAutoActivate = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	PrimaryComponentTick.bCanEverTick = true;
}

void UMLDeformerComponent::Init()
{
	using namespace UE::MLDeformer;

	// If there is no deformer asset linked, release what we currently have.
	if (DeformerAsset == nullptr)
	{
		if (ModelInstance)
		{
			ModelInstance->Release();
		}
		return;
	}

	// Try to initialize the deformer model.
	UMLDeformerModel* Model = DeformerAsset->GetModel();
	if (Model)
	{
		// Create the model instance if it isn't there yet.
		if (ModelInstance == nullptr)
		{
			ModelInstance = TUniquePtr<FMLDeformerModelInstance>(Model->CreateModelInstance());
		}
		else
		{
			ModelInstance->Release();
		}
		ModelInstance->Init(SkelMeshComponent);
	}
	else
	{
		UE_LOG(LogMLDeformer, Warning, TEXT("ML Deformer component on '%s' has a deformer asset that has no ML model setup."), *GetOuter()->GetName());
	}
}

void UMLDeformerComponent::SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent)
{
	AddTickPrerequisiteComponent(SkelMeshComponent);

	DeformerAsset = InDeformerAsset;
	SkelMeshComponent = InSkelMeshComponent;

	// Initialize and make sure we have a model instance.
	RemoveNeuralNetworkModifyDelegate();
	Init();
	AddNeuralNetworkModifyDelegate();
}

void UMLDeformerComponent::AddNeuralNetworkModifyDelegate()
{
	if (DeformerAsset == nullptr)
	{
		return;
	}

	UMLDeformerModel* Model = DeformerAsset->GetModel();
	if (Model)
	{
		NeuralNetworkModifyDelegateHandle = Model->NeuralNetworkModifyDelegate.AddLambda
		(
			([this]()
			{
				ModelInstance->Release();
				ModelInstance->Init(SkelMeshComponent);
			})
		);
	}
}

void UMLDeformerComponent::RemoveNeuralNetworkModifyDelegate()
{
	if (DeformerAsset != nullptr && 
		NeuralNetworkModifyDelegateHandle != FDelegateHandle() && 
		DeformerAsset->GetModel() != nullptr)
	{
		DeformerAsset->GetModel()->NeuralNetworkModifyDelegate.Remove(NeuralNetworkModifyDelegateHandle);
	}
	
	NeuralNetworkModifyDelegateHandle = FDelegateHandle();
}

void UMLDeformerComponent::BeginDestroy()
{
	RemoveNeuralNetworkModifyDelegate();
	Super::BeginDestroy();
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
	if (ModelInstance)
	{
		ModelInstance->Release();
	}
}

void UMLDeformerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (TickType != ELevelTick::LEVELTICK_PauseTick)
	{
		if (ModelInstance &&
			SkelMeshComponent && 
			SkelMeshComponent->GetPredictedLODLevel() == 0)
		{
			ModelInstance->Tick(DeltaTime);
		}
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
