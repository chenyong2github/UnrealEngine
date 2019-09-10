// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/TransformGizmo.h"
#include "InteractiveGizmoManager.h"
#include "BaseGizmos/AxisPositionGizmo.h"
#include "BaseGizmos/PlanePositionGizmo.h"
#include "BaseGizmos/AxisAngleGizmo.h"
#include "BaseGizmos/GizmoComponents.h"

#include "BaseGizmos/GizmoArrowComponent.h"
#include "BaseGizmos/GizmoRectangleComponent.h"
#include "BaseGizmos/GizmoCircleComponent.h"

// need this to implement hover
#include "BaseGizmos/GizmoBaseComponent.h"

#include "Components/SphereComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"


#define LOCTEXT_NAMESPACE "UTransformGizmo"


ATransformGizmoActor::ATransformGizmoActor()
{
	// root component is a hidden sphere
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("GizmoCenter"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}



ATransformGizmoActor* ATransformGizmoActor::ConstructDefault3AxisGizmo(UWorld* World)
{
	FActorSpawnParameters SpawnInfo;
	ATransformGizmoActor* NewActor = World->SpawnActor<ATransformGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	NewActor->TranslateX = AddDefaultArrowComponent(World, NewActor, FLinearColor::Red, FVector(1, 0, 0));
	NewActor->TranslateY = AddDefaultArrowComponent(World, NewActor, FLinearColor::Green, FVector(0, 1, 0));
	NewActor->TranslateZ = AddDefaultArrowComponent(World, NewActor, FLinearColor::Blue, FVector(0, 0, 1));

	NewActor->TranslateYZ = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Red, FVector(0,1,0), FVector(0,0,1));
	NewActor->TranslateXZ = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Green, FVector(1, 0, 0), FVector(0, 0, 1));
	NewActor->TranslateXY = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Blue, FVector(1, 0, 0), FVector(0, 1, 0));

	NewActor->RotateX = AddDefaultCircleComponent(World, NewActor, FLinearColor::Red, FVector(1, 0, 0));
	NewActor->RotateY = AddDefaultCircleComponent(World, NewActor, FLinearColor::Green, FVector(0, 1, 0));
	NewActor->RotateZ = AddDefaultCircleComponent(World, NewActor, FLinearColor::Blue, FVector(0, 0, 1));


	// add a non-interactive view-aligned circle element, so the axes look like a sphere.
	UGizmoCircleComponent* SphereEdge = NewObject<UGizmoCircleComponent>(NewActor);
	NewActor->AddInstanceComponent(SphereEdge);
	SphereEdge->AttachToComponent(NewActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	SphereEdge->Color = FLinearColor::Gray;
	SphereEdge->Thickness = 1.0f;
	SphereEdge->bViewAligned = true;
	SphereEdge->RegisterComponent();


	return NewActor;
}





ATransformGizmoActor* FTransformGizmoActorFactory::CreateNewGizmoActor(UWorld* World) const
{
	return ATransformGizmoActor::ConstructDefault3AxisGizmo(World);
}



UInteractiveGizmo* UTransformGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UTransformGizmo* NewGizmo = NewObject<UTransformGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);

	// use default gizmo actor if client has not given us a new builder
	NewGizmo->SetGizmoActorBuilder( (GizmoActorBuilder) ? GizmoActorBuilder : MakeShared<FTransformGizmoActorFactory>() );

	// override default hover function if proposed
	if (UpdateHoverFunction)
	{
		NewGizmo->SetUpdateHoverFunction(UpdateHoverFunction);
	}

	return NewGizmo;
}



void UTransformGizmo::SetWorld(UWorld* WorldIn)
{
	this->World = WorldIn;
}


void UTransformGizmo::SetGizmoActorBuilder(TSharedPtr<FTransformGizmoActorFactory> Builder)
{
	GizmoActorBuilder = Builder;
}

void UTransformGizmo::SetUpdateHoverFunction(TFunction<void(UPrimitiveComponent*, bool)> HoverFunction)
{
	UpdateHoverFunction = HoverFunction;
}


void UTransformGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	UpdateHoverFunction = [](UPrimitiveComponent* Component, bool bHovering)
	{
		if (Cast<UGizmoBaseComponent>(Component) != nullptr)
		{
			Cast<UGizmoBaseComponent>(Component)->UpdateHoverState(bHovering);
		}
	};

	GizmoActor = GizmoActorBuilder->CreateNewGizmoActor(World);
}



void UTransformGizmo::Shutdown()
{
	ClearActiveTarget();

	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
}


void UTransformGizmo::SetActiveTarget(UTransformProxy* Target)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}

	ActiveTarget = Target;

	// move gizmo to target location
	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();

	FTransform TargetTransform = Target->GetTransform();
	FVector SaveScale = TargetTransform.GetScale3D();
	TargetTransform.SetScale3D(FVector(1, 1, 1));
	GizmoComponent->SetWorldTransform(TargetTransform);

	// target tracks location of GizmoComponent
	GizmoActor->GetRootComponent()->TransformUpdated.AddLambda(
		[this, SaveScale](USceneComponent* Component, EUpdateTransformFlags /*UpdateTransformFlags*/, ETeleportType /*Teleport*/) {
		//this->GetGizmoManager()->PostMessage(TEXT("TRANSFORM UPDATED"), EToolMessageLevel::Internal);
		FTransform NewXForm = Component->GetComponentToWorld();
		NewXForm.SetScale3D(SaveScale);
		this->ActiveTarget->SetTransform(NewXForm);
	});


	UGizmoObjectModifyStateTarget* StateTarget = UGizmoObjectModifyStateTarget::Construct(GizmoComponent,
		LOCTEXT("UTransformGizmoTransaction", "Transform"), GetGizmoManager(), this);

	UGizmoComponentWorldTransformSource* TransformSource = 
		UGizmoComponentWorldTransformSource::Construct(GizmoComponent, this);

	// root component provides local X/Y/Z axis, identified by AxisIndex
	AxisXSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 0, true, this);
	AxisYSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 1, true, this);
	AxisZSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 2, true, this);

	// todo should we hold onto these?
	if (GizmoActor->TranslateX != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateX, GizmoComponent, TEXT("TranslateX"), AxisXSource, TransformSource, StateTarget);
	}
	if (GizmoActor->TranslateY != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateY, GizmoComponent, TEXT("TranslateY"), AxisYSource, TransformSource, StateTarget);
	}
	if (GizmoActor->TranslateZ != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateZ, GizmoComponent, TEXT("TranslateZ"), AxisZSource, TransformSource, StateTarget);
	}


	if (GizmoActor->TranslateYZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateYZ, GizmoComponent, TEXT("TranslateYZ"), AxisXSource, TransformSource, StateTarget);
	}
	if (GizmoActor->TranslateXZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXZ, GizmoComponent, TEXT("TranslateXZ"), AxisYSource, TransformSource, StateTarget);
	}
	if (GizmoActor->TranslateXY != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXY, GizmoComponent, TEXT("TranslateXY"), AxisZSource, TransformSource, StateTarget);
	}

	if (GizmoActor->RotateX != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateX, GizmoComponent, TEXT("RotateX"), AxisXSource, TransformSource, StateTarget);
	}
	if (GizmoActor->RotateY != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateY, GizmoComponent, TEXT("RotateY"), AxisYSource, TransformSource, StateTarget);
	}
	if (GizmoActor->RotateZ != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateZ, GizmoComponent, TEXT("RotateZ"), AxisZSource, TransformSource, StateTarget);
	}
}



UInteractiveGizmo* UTransformGizmo::AddAxisTranslationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	const FString& Identifier,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTarget)
{
	// create axis-position gizmo, axis-position parameter will drive translation
	UAxisPositionGizmo* TranslateGizmo = Cast<UAxisPositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisPositionBuilderIdentifier, Identifier));
	check(TranslateGizmo);

	// axis source provides the translation axis
	TranslateGizmo->AxisSource = Cast<UObject>(AxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	TranslateGizmo->ParameterSource = UGizmoAxisTranslationParameterSource::Construct(AxisSource, TransformSource, this);

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(AxisComponent, this);
	if (this->UpdateHoverFunction)
	{
		HitTarget->UpdateHoverFunction = [AxisComponent, this](bool bHovering) { this->UpdateHoverFunction(AxisComponent, bHovering); };
	}
	TranslateGizmo->HitTarget = HitTarget;

	TranslateGizmo->StateTarget = Cast<UObject>(StateTarget);

	ActiveGizmos.Add(TranslateGizmo);
	return TranslateGizmo;
}



UInteractiveGizmo* UTransformGizmo::AddPlaneTranslationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	const FString& Identifier,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTarget)
{
	// create axis-position gizmo, axis-position parameter will drive translation
	UPlanePositionGizmo* TranslateGizmo = Cast<UPlanePositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultPlanePositionBuilderIdentifier, Identifier));
	check(TranslateGizmo);

	// axis source provides the translation axis
	TranslateGizmo->AxisSource = Cast<UObject>(AxisSource);

	// parameter source maps axis-parameter-change to translation of TransformSource's transform
	TranslateGizmo->ParameterSource = UGizmoPlaneTranslationParameterSource::Construct(AxisSource, TransformSource, this);

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(AxisComponent, this);
	if (this->UpdateHoverFunction)
	{
		HitTarget->UpdateHoverFunction = [AxisComponent, this](bool bHovering) { this->UpdateHoverFunction(AxisComponent, bHovering); };
	}
	TranslateGizmo->HitTarget = HitTarget;

	TranslateGizmo->StateTarget = Cast<UObject>(StateTarget);

	ActiveGizmos.Add(TranslateGizmo);
	return TranslateGizmo;
}





UInteractiveGizmo* UTransformGizmo::AddAxisRotationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	const FString& Identifier,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTarget)
{
	// create axis-angle gizmo, angle will drive axis-rotation
	UAxisAngleGizmo* RotateGizmo = Cast<UAxisAngleGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisAngleBuilderIdentifier, Identifier));
	check(RotateGizmo);

	// axis source provides the rotation axis
	RotateGizmo->AxisSource = Cast<UObject>(AxisSource);

	// parameter source maps angle-parameter-change to rotation of TransformSource's transform
	RotateGizmo->AngleSource = UGizmoAxisRotationParameterSource::Construct(AxisSource, TransformSource, this);

	// sub-component provides hit target
	UGizmoComponentHitTarget* HitTarget = UGizmoComponentHitTarget::Construct(AxisComponent, this);
	if (this->UpdateHoverFunction)
	{
		HitTarget->UpdateHoverFunction = [AxisComponent, this](bool bHovering) { this->UpdateHoverFunction(AxisComponent, bHovering); };
	}
	RotateGizmo->HitTarget = HitTarget;

	RotateGizmo->StateTarget = Cast<UObject>(StateTarget);

	ActiveGizmos.Add(RotateGizmo);

	return RotateGizmo;
}





void UTransformGizmo::ClearActiveTarget()
{
	for (UInteractiveGizmo* Gizmo : ActiveGizmos)
	{
		GetGizmoManager()->DestroyGizmo(Gizmo);
	}
	ActiveGizmos.SetNum(0);

	ActiveTarget = nullptr;
}



#undef LOCTEXT_NAMESPACE