// Copyright Epic Games, Inc. All Rights Reserved.

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
	return ConstructCustom3AxisGizmo(World, 
		ETransformGizmoSubElements::TranslateAllAxes |
		ETransformGizmoSubElements::TranslateAllPlanes |
		ETransformGizmoSubElements::RotateAllAxes);
}


ATransformGizmoActor* ATransformGizmoActor::ConstructCustom3AxisGizmo(
	UWorld* World,
	ETransformGizmoSubElements Elements)
{
	FActorSpawnParameters SpawnInfo;
	ATransformGizmoActor* NewActor = World->SpawnActor<ATransformGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	if ((Elements & ETransformGizmoSubElements::TranslateAxisX) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateX = AddDefaultArrowComponent(World, NewActor, FLinearColor::Red, FVector(1, 0, 0));
	}
	if ((Elements & ETransformGizmoSubElements::TranslateAxisY) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateY = AddDefaultArrowComponent(World, NewActor, FLinearColor::Green, FVector(0, 1, 0));
	}
	if ((Elements & ETransformGizmoSubElements::TranslateAxisZ) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateZ = AddDefaultArrowComponent(World, NewActor, FLinearColor::Blue, FVector(0, 0, 1));
	}

	if ((Elements & ETransformGizmoSubElements::TranslatePlaneYZ) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateYZ = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Red, FVector(0, 1, 0), FVector(0, 0, 1));
	}
	if ((Elements & ETransformGizmoSubElements::TranslatePlaneXZ) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateXZ = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Green, FVector(1, 0, 0), FVector(0, 0, 1));
	}
	if ((Elements & ETransformGizmoSubElements::TranslatePlaneXY) != ETransformGizmoSubElements::None)
	{
		NewActor->TranslateXY = AddDefaultRectangleComponent(World, NewActor, FLinearColor::Blue, FVector(1, 0, 0), FVector(0, 1, 0));
	}

	bool bAnyRotate = false;
	if ((Elements & ETransformGizmoSubElements::RotateAxisX) != ETransformGizmoSubElements::None)
	{
		NewActor->RotateX = AddDefaultCircleComponent(World, NewActor, FLinearColor::Red, FVector(1, 0, 0));
		bAnyRotate = true;
	}
	if ((Elements & ETransformGizmoSubElements::RotateAxisY) != ETransformGizmoSubElements::None)
	{
		NewActor->RotateY = AddDefaultCircleComponent(World, NewActor, FLinearColor::Green, FVector(0, 1, 0));
		bAnyRotate = true;
	}
	if ((Elements & ETransformGizmoSubElements::RotateAxisZ) != ETransformGizmoSubElements::None)
	{
		NewActor->RotateZ = AddDefaultCircleComponent(World, NewActor, FLinearColor::Blue, FVector(0, 0, 1));
		bAnyRotate = true;
	}


	// add a non-interactive view-aligned circle element, so the axes look like a sphere.
	if (bAnyRotate)
	{
		UGizmoCircleComponent* SphereEdge = NewObject<UGizmoCircleComponent>(NewActor);
		NewActor->AddInstanceComponent(SphereEdge);
		SphereEdge->AttachToComponent(NewActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		SphereEdge->Color = FLinearColor::Gray;
		SphereEdge->Thickness = 1.0f;
		SphereEdge->bViewAligned = true;
		SphereEdge->RegisterComponent();
	}

	return NewActor;
}




ATransformGizmoActor* FTransformGizmoActorFactory::CreateNewGizmoActor(UWorld* World) const
{
	return ATransformGizmoActor::ConstructCustom3AxisGizmo(World, EnableElements);
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

	if (UpdateCoordSystemFunction)
	{
		NewGizmo->SetUpdateCoordSystemFunction(UpdateCoordSystemFunction);
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

void UTransformGizmo::SetUpdateCoordSystemFunction(TFunction<void(UPrimitiveComponent*, EToolContextCoordinateSystem)> CoordSysFunction)
{
	UpdateCoordSystemFunction = CoordSysFunction;
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

	UpdateCoordSystemFunction = [](UPrimitiveComponent* Component, EToolContextCoordinateSystem CoordSystem)
	{
		if (Cast<UGizmoBaseComponent>(Component) != nullptr)
		{
			Cast<UGizmoBaseComponent>(Component)->UpdateWorldLocalState(CoordSystem == EToolContextCoordinateSystem::World);
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



void UTransformGizmo::Tick(float DeltaTime)
{	
	EToolContextCoordinateSystem CoordSystem = GetGizmoManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem();
	check(CoordSystem == EToolContextCoordinateSystem::World || CoordSystem == EToolContextCoordinateSystem::Local)
	bool bUseLocalAxes =
		(GetGizmoManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::Local);
	if (AxisXSource != nullptr && AxisYSource != nullptr && AxisZSource != nullptr)
	{
		AxisXSource->bLocalAxes = bUseLocalAxes;
		AxisYSource->bLocalAxes = bUseLocalAxes;
		AxisZSource->bLocalAxes = bUseLocalAxes;
	}
	if (UpdateCoordSystemFunction)
	{
		for (UPrimitiveComponent* Component : ActiveComponents)
		{
			UpdateCoordSystemFunction(Component, CoordSystem);
		}
	}
}



void UTransformGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
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

	// Target tracks location of GizmoComponent. Note that TransformUpdated is not called during undo/redo transactions!
	// We currently rely on the transaction system to undo/redo target object locations. This will not work during runtime...
	GizmoComponent->TransformUpdated.AddLambda(
		[this, SaveScale](USceneComponent* Component, EUpdateTransformFlags /*UpdateTransformFlags*/, ETeleportType /*Teleport*/) {
		//this->GetGizmoManager()->DisplayMessage(TEXT("TRANSFORM UPDATED"), EToolMessageLevel::Internal);
		FTransform NewXForm = Component->GetComponentToWorld();
		NewXForm.SetScale3D(SaveScale);
		this->ActiveTarget->SetTransform(NewXForm);
	});


	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}
	StateTarget = UGizmoTransformChangeStateTarget::Construct(GizmoComponent,
		LOCTEXT("UTransformGizmoTransaction", "Transform"), TransactionProvider, this);
	StateTarget->DependentChangeSources.Add(MakeUnique<FTransformProxyChangeSource>(Target));

	UGizmoComponentWorldTransformSource* TransformSource = 
		UGizmoComponentWorldTransformSource::Construct(GizmoComponent, this);

	// root component provides local X/Y/Z axis, identified by AxisIndex
	AxisXSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 0, true, this);
	AxisYSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 1, true, this);
	AxisZSource = UGizmoComponentAxisSource::Construct(GizmoComponent, 2, true, this);

	// todo should we hold onto these?
	if (GizmoActor->TranslateX != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateX, GizmoComponent, AxisXSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateX);
	}
	if (GizmoActor->TranslateY != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateY, GizmoComponent, AxisYSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateY);
	}
	if (GizmoActor->TranslateZ != nullptr)
	{
		AddAxisTranslationGizmo(GizmoActor->TranslateZ, GizmoComponent, AxisZSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateZ);
	}


	if (GizmoActor->TranslateYZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateYZ, GizmoComponent, AxisXSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateYZ);
	}
	if (GizmoActor->TranslateXZ != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXZ, GizmoComponent, AxisYSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateXZ);
	}
	if (GizmoActor->TranslateXY != nullptr)
	{
		AddPlaneTranslationGizmo(GizmoActor->TranslateXY, GizmoComponent, AxisZSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->TranslateXY);
	}

	if (GizmoActor->RotateX != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateX, GizmoComponent, AxisXSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->RotateX);
	}
	if (GizmoActor->RotateY != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateY, GizmoComponent, AxisYSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->RotateY);
	}
	if (GizmoActor->RotateZ != nullptr)
	{
		AddAxisRotationGizmo(GizmoActor->RotateZ, GizmoComponent, AxisZSource, TransformSource, StateTarget);
		ActiveComponents.Add(GizmoActor->RotateZ);
	}
}



void UTransformGizmo::SetNewGizmoTransform(const FTransform& NewTransform)
{
	check(ActiveTarget != nullptr);

	StateTarget->BeginUpdate();

	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();
	GizmoComponent->SetWorldTransform(NewTransform);
	//ActiveTarget->SetTransform(NewTransform);		// this will happen in the GizmoComponent.TransformUpdated delegate handler above

	StateTarget->EndUpdate();
}


UInteractiveGizmo* UTransformGizmo::AddAxisTranslationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTargetIn)
{
	// create axis-position gizmo, axis-position parameter will drive translation
	UAxisPositionGizmo* TranslateGizmo = Cast<UAxisPositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisPositionBuilderIdentifier));
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

	TranslateGizmo->StateTarget = Cast<UObject>(StateTargetIn);

	ActiveGizmos.Add(TranslateGizmo);
	return TranslateGizmo;
}



UInteractiveGizmo* UTransformGizmo::AddPlaneTranslationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTargetIn)
{
	// create axis-position gizmo, axis-position parameter will drive translation
	UPlanePositionGizmo* TranslateGizmo = Cast<UPlanePositionGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultPlanePositionBuilderIdentifier));
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

	TranslateGizmo->StateTarget = Cast<UObject>(StateTargetIn);

	ActiveGizmos.Add(TranslateGizmo);
	return TranslateGizmo;
}





UInteractiveGizmo* UTransformGizmo::AddAxisRotationGizmo(
	UPrimitiveComponent* AxisComponent, USceneComponent* RootComponent,
	IGizmoAxisSource* AxisSource,
	IGizmoTransformSource* TransformSource,
	IGizmoStateTarget* StateTargetIn)
{
	// create axis-angle gizmo, angle will drive axis-rotation
	UAxisAngleGizmo* RotateGizmo = Cast<UAxisAngleGizmo>(GetGizmoManager()->CreateGizmo(
		UInteractiveGizmoManager::DefaultAxisAngleBuilderIdentifier));
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

	RotateGizmo->StateTarget = Cast<UObject>(StateTargetIn);

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
	ActiveComponents.SetNum(0);

	AxisXSource = nullptr;
	AxisYSource = nullptr;
	AxisZSource = nullptr;
	StateTarget = nullptr;

	ActiveTarget = nullptr;
}



#undef LOCTEXT_NAMESPACE