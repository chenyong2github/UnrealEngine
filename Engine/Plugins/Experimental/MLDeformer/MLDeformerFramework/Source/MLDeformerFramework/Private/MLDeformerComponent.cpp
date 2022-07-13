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
	// If there is no deformer asset linked, release what we currently have.
	if (DeformerAsset == nullptr)
	{
		ModelInstance = nullptr;
		return;
	}

	// Try to initialize the deformer model.
	UMLDeformerModel* Model = DeformerAsset->GetModel();
	if (Model)
	{
		if (ModelInstance)
		{
			ModelInstance->Release();
		}
		ModelInstance = Model->CreateModelInstance(this);
		ModelInstance->SetModel(Model);
		ModelInstance->Init(SkelMeshComponent);
		Model->PostMLDeformerComponentInit(ModelInstance);
	}
	else
	{
		ModelInstance = nullptr;
		UE_LOG(LogMLDeformer, Warning, TEXT("ML Deformer component on '%s' has a deformer asset that has no ML model setup."), *GetOuter()->GetName());
	}
}

void UMLDeformerComponent::SetupComponent(UMLDeformerAsset* InDeformerAsset, USkeletalMeshComponent* InSkelMeshComponent)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerComponent::SetupComponent)

	if (InSkelMeshComponent)
	{		
		AddTickPrerequisiteComponent(InSkelMeshComponent);
	}

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
				Init();
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
	ModelInstance = nullptr;
}

void UMLDeformerComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (TickType != ELevelTick::LEVELTICK_PauseTick)
	{
		if (ModelInstance &&
			SkelMeshComponent && 
			SkelMeshComponent->GetPredictedLODLevel() == 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UMLDeformerComponent::TickComponent)
			ModelInstance->Tick(DeltaTime, Weight);
		}
	}
}

#if WITH_EDITOR
	void UMLDeformerComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UMLDeformerComponent, DeformerAsset))
		{
			RemoveNeuralNetworkModifyDelegate();
			Init();
			AddNeuralNetworkModifyDelegate();
		}
	}
#endif
