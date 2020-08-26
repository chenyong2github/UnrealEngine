// Copyright Epic Games, Inc. All Rights Reserved.

#include "SpotLightGizmo.h"
#include "Components/SpotLightComponent.h"
#include "ScalableConeGizmo.h"
#include "LightGizmosModule.h"
#include "Engine/CollisionProfile.h"
#include "Kismet/KismetMathLibrary.h"
#include "BaseGizmos/GizmoMath.h"
#include "Components/SphereComponent.h"
#include "BaseGizmos/GizmoLineHandleComponent.h"

// USpotLightGizmoBuilder

UInteractiveGizmo* USpotLightGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	USpotLightGizmo* NewGizmo = NewObject<USpotLightGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	return NewGizmo;
}

// ASpotLightGizmoActor

ASpotLightGizmoActor::ASpotLightGizmoActor()
{
	// root component is a hidden sphere
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("GizmoCenter"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

}

// USpotLightGizmo

USpotLightGizmo::USpotLightGizmo()
{
	LightActor = nullptr;
	TransformProxy = nullptr;
}

void USpotLightGizmo::Setup()
{
	USpotLightGizmoInputBehavior* SpotLightBehavior = NewObject<USpotLightGizmoInputBehavior>(this);
	SpotLightBehavior->Initialize(this);
	AddInputBehavior(SpotLightBehavior);
}

void USpotLightGizmo::Tick(float DeltaTime)
{
	// Make sure the gizmos are up to date with the various light properties 
	if (OuterAngleGizmo)
	{
		OuterAngleGizmo->SetAngleDegrees(LightActor->SpotLightComponent->OuterConeAngle);
		OuterAngleGizmo->SetLength(LightActor->SpotLightComponent->AttenuationRadius);
	}

	if (InnerAngleGizmo)
	{
		InnerAngleGizmo->SetAngleDegrees(LightActor->SpotLightComponent->InnerConeAngle);
		InnerAngleGizmo->SetLength(LightActor->SpotLightComponent->AttenuationRadius);
	}

	if (GizmoActor)
	{
		GizmoActor->AttenuationScaleHandle->SetRelativeLocation(FVector(LightActor->SpotLightComponent->AttenuationRadius, 0, 0));
	}
}

void USpotLightGizmo::Shutdown()
{
	if (OuterAngleGizmo)
	{
		GetGizmoManager()->DestroyGizmo(OuterAngleGizmo);
		OuterAngleGizmo = nullptr;
	}

	if (InnerAngleGizmo)
	{
		GetGizmoManager()->DestroyGizmo(InnerAngleGizmo);
		InnerAngleGizmo = nullptr;
	}

	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
}

void USpotLightGizmo::SetSelectedObject(ASpotLight* InLight)
{
	LightActor = InLight;

	// TODO: No way to remove a component from the transform proxy
	if (!TransformProxy)
	{
		TransformProxy = NewObject<USubTransformProxy>(this);
	}

	USceneComponent* SceneComponent = LightActor->GetRootComponent();
	TransformProxy->AddComponent(SceneComponent);
}

USubTransformProxy* USpotLightGizmo::GetTransformProxy()
{
	return TransformProxy;
}

void USpotLightGizmo::OnOuterAngleUpdate(float NewAngle)
{
	LightActor->SpotLightComponent->OuterConeAngle = NewAngle;
	
	// OuterAngle cannot be less than Inner Angle
	if (NewAngle < LightActor->SpotLightComponent->InnerConeAngle)
	{
		LightActor->SpotLightComponent->InnerConeAngle = NewAngle;
	}

	LightActor->SpotLightComponent->MarkRenderStateDirty();

}

void USpotLightGizmo::OnInnerAngleUpdate(float NewAngle)
{
	LightActor->SpotLightComponent->InnerConeAngle = NewAngle;

	// Inner Angle cannot be greater than Outer Angle
	if (NewAngle > LightActor->SpotLightComponent->OuterConeAngle)
	{
		LightActor->SpotLightComponent->OuterConeAngle = NewAngle;
	}

	LightActor->SpotLightComponent->MarkRenderStateDirty();
}

void USpotLightGizmo::OnTransformChanged(UTransformProxy*, FTransform)
{
	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();

	FTransform TargetTransform = TransformProxy->GetTransform();

	// The gizmo doesn't want the scale of the target
	TargetTransform.SetScale3D(FVector(1, 1, 1));

	GizmoComponent->SetWorldTransform(TargetTransform);
}

void USpotLightGizmo::CreateOuterAngleGizmo()
{
	if (!LightActor)
	{
		return;
	}

	OuterAngleGizmo = Cast<UScalableConeGizmo>(GetGizmoManager()->CreateGizmo(FLightGizmosModule::ScalableConeGizmoType));
	OuterAngleGizmo->SetTarget(TransformProxy);
	OuterAngleGizmo->UpdateAngleFunc = [this](float NewAngle) { this->OnOuterAngleUpdate(NewAngle); };
	OuterAngleGizmo->MaxAngle = 80.f;
	OuterAngleGizmo->MinAngle = 1.f;
}

void USpotLightGizmo::CreateInnerAngleGizmo()
{
	if (!LightActor)
	{
		return;
	}

	InnerAngleGizmo = Cast<UScalableConeGizmo>(GetGizmoManager()->CreateGizmo(FLightGizmosModule::ScalableConeGizmoType));
	InnerAngleGizmo->SetTarget(TransformProxy);
	InnerAngleGizmo->UpdateAngleFunc = [this](float NewAngle) { this->OnInnerAngleUpdate(NewAngle); };
	InnerAngleGizmo->MaxAngle = 80.f;
	InnerAngleGizmo->MinAngle = 1.f;
	InnerAngleGizmo->ConeColor = FColor(150, 200, 255);
}

void USpotLightGizmo::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void USpotLightGizmo::OnBeginDrag(const FInputDeviceRay& Ray)
{
	FVector Start = Ray.WorldRay.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.WorldRay.Origin + Ray.WorldRay.Direction * MaxRaycastDistance;

	FRay HitCheckRay(Start, End - Start);
	FHitResult HitResult;
	FVector HitAxis;
	FTransform DragTransform;

	// Check if any of the components were hit
	if (HitTest(HitCheckRay, HitResult, DragTransform))
	{
		FVector RayNearestPt; float RayNearestParam;
		FVector InteractionStartPoint;

		// Update interaction start parameters
		GizmoMath::NearestPointOnLineToRay(DragTransform.GetLocation(), GizmoActor->GetActorForwardVector(),
			Ray.WorldRay.Origin, Ray.WorldRay.Direction,
			InteractionStartPoint, InteractionStartParameter,
			RayNearestPt, RayNearestParam);

		DragStartWorldPosition = DragTransform.GetLocation();
	}
}

void USpotLightGizmo::OnUpdateDrag(const FInputDeviceRay& Ray)
{
	FVector AxisNearestPt; float AxisNearestParam;
	FVector RayNearestPt; float RayNearestParam;

	// Get current interaction parameters
	GizmoMath::NearestPointOnLineToRay(DragStartWorldPosition, GizmoActor->GetActorForwardVector(),
		Ray.WorldRay.Origin, Ray.WorldRay.Direction,
		AxisNearestPt, AxisNearestParam,
		RayNearestPt, RayNearestParam);

	float InteractionCurParameter = AxisNearestParam;

	float DeltaParam = InteractionCurParameter - InteractionStartParameter;

	InteractionStartParameter = InteractionCurParameter;

	// Update the attenuation of the cone
	float NewAttenuation = LightActor->SpotLightComponent->AttenuationRadius + DeltaParam;

	NewAttenuation = (NewAttenuation < 0) ? 0 : NewAttenuation;

	LightActor->SpotLightComponent->AttenuationRadius = NewAttenuation;
	LightActor->SpotLightComponent->MarkRenderStateDirty();

	if (OuterAngleGizmo)
	{
		OuterAngleGizmo->SetLength(NewAttenuation);
	}

	if (InnerAngleGizmo)
	{
		InnerAngleGizmo->SetLength(NewAttenuation);
	}
}

bool USpotLightGizmo::HitTest(const FRay& Ray, FHitResult& OutHit, FTransform& OutTransform)
{
	if (!GizmoActor)
	{
		return false;
	}

	FVector Start = Ray.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.Origin + Ray.Direction * MaxRaycastDistance;

	FCollisionQueryParams Params;

	if (GizmoActor->AttenuationScaleHandle->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutTransform = GizmoActor->AttenuationScaleHandle->GetComponentTransform();
		return true;
	}

	return false;
}

void USpotLightGizmo::CreateAttenuationScaleGizmo()
{
	if (!OuterAngleGizmo)
	{
		return;
	}

	FActorSpawnParameters SpawnInfo;
	GizmoActor = World->SpawnActor<ASpotLightGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	// The handle to scale attenuation is line handle component
	GizmoActor->AttenuationScaleHandle = AGizmoActor::AddDefaultLineHandleComponent(World, GizmoActor, FLinearColor::Blue, FVector::YAxisVector, FVector::XAxisVector, 60.f, true);
	GizmoActor->AttenuationScaleHandle->SetRelativeLocation(FVector(LightActor->SpotLightComponent->AttenuationRadius, 0, 0));

	TransformProxy->OnTransformChanged.AddUObject(this, &USpotLightGizmo::OnTransformChanged);

	OnTransformChanged(TransformProxy, TransformProxy->GetTransform());
}

// USpotLightGizmoInputBehavior

void USpotLightGizmoInputBehavior::Initialize(USpotLightGizmo* InGizmo)
{
	Gizmo = InGizmo;
}

FInputCaptureRequest USpotLightGizmoInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input))
	{
		FHitResult HitResult;
		FTransform DragTransform;
		if (Gizmo->HitTest(input.Mouse.WorldRay, HitResult, DragTransform))
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.Distance);
		}
	}

	return FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate USpotLightGizmoInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	Gizmo->OnBeginDrag(DeviceRay);
	bInputDragCaptured = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate USpotLightGizmoInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	if (IsReleased(input))
	{
		bInputDragCaptured = false;
		return FInputCaptureUpdate::End();
	}

	Gizmo->OnUpdateDrag(LastWorldRay);

	return FInputCaptureUpdate::Continue();
}

void USpotLightGizmoInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInputDragCaptured)
	{
		bInputDragCaptured = false;
	}
}