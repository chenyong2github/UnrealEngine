// Copyright Epic Games, Inc. All Rights Reserved.

#include "DirectionalLightGizmo.h"
#include "BaseGizmos/GizmoBoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "Kismet/KismetMathLibrary.h"
#include "BaseGizmos/GizmoMath.h"
#include "BaseGizmos/GizmoCircleComponent.h"
#include "BaseGizmos/GizmoLineHandleComponent.h"

// UDirectionalLightGizmoBuilder

UInteractiveGizmo* UDirectionalLightGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UDirectionalLightGizmo* NewGizmo = NewObject<UDirectionalLightGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	return NewGizmo;
}

// ADirectionalLightGizmoActor

ADirectionalLightGizmoActor::ADirectionalLightGizmoActor()
{
	// root component is a hidden sphere
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("GizmoCenter"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

}

// UDirectionalLightGizmo

UDirectionalLightGizmo::UDirectionalLightGizmo()
{
	LightActor = nullptr;
	GizmoActor = nullptr;
	TransformProxy = nullptr;
}

void UDirectionalLightGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	UDirectionalLightGizmoInputBehavior* DirectionalLightBehavior = NewObject<UDirectionalLightGizmoInputBehavior>(this);
	DirectionalLightBehavior->Initialize(this);
	AddInputBehavior(DirectionalLightBehavior);

	CreateGizmoHandles();

	CreateZRotationGizmo();

	// The Gizmo is being rotated around the Y axis
	RotationPlaneX = FVector::XAxisVector;
	RotationPlaneZ = FVector::ZAxisVector;
	
}

void UDirectionalLightGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	
}

void UDirectionalLightGizmo::Shutdown()
{
	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
}

USubTransformProxy* UDirectionalLightGizmo::GetTransformProxy()
{
	return TransformProxy;
}

void UDirectionalLightGizmo::SetSelectedObject(ADirectionalLight* InLight)
{
	LightActor = InLight;

	// TODO: Cannot remove a component from Transform Proxy
	if (!TransformProxy)
	{
		TransformProxy = NewObject<USubTransformProxy>(this);
	}

	USceneComponent* SceneComponent = LightActor->GetRootComponent();

	TransformProxy->AddComponent(SceneComponent);

	TransformProxy->OnTransformChanged.AddUObject(this, &UDirectionalLightGizmo::OnTransformChanged);

	OnTransformChanged(TransformProxy, TransformProxy->GetTransform());

}

void UDirectionalLightGizmo::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void UDirectionalLightGizmo::OnBeginDrag(const FInputDeviceRay& Ray)
{
	FVector Start = Ray.WorldRay.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.WorldRay.Origin + Ray.WorldRay.Direction * MaxRaycastDistance;

	FRay HitCheckRay(Start, End - Start);
	FHitResult HitResult;

	FTransform DragTransform;
	HitComponent = nullptr;

	// Check if any component was hit
	if (HitTest(HitCheckRay, HitResult, DragTransform, HitComponent))
	{
		// Rotate around y axis if the arrow was hit
		if (HitComponent == GizmoActor->Arrow)
		{
			HitAxis = FVector::YAxisVector;

			// Get the rotated plane vectors for the interaction
			GizmoMath::MakeNormalPlaneBasis(HitAxis, RotationPlaneX, RotationPlaneZ);
			RotationPlaneX = LightActor->GetActorRotation().RotateVector(FVector::XAxisVector);
			RotationPlaneZ = LightActor->GetActorRotation().RotateVector(FVector::ZAxisVector);

		}
		// Rotate around Z axis if the circle was hit
		else
		{
			HitAxis = FVector::ZAxisVector;
			RotationPlaneX = FVector::XAxisVector;
			RotationPlaneZ = FVector::YAxisVector;
		}

		// Calculate initial hit position
		DragStartWorldPosition = GizmoMath::ProjectPointOntoLine(Ray.WorldRay.PointAt(HitResult.Distance), DragTransform.GetLocation(), HitAxis);

		// Calculate initial hit parameters
		bool bIntersects; 
		FVector IntersectionPoint;

		GizmoMath::RayPlaneIntersectionPoint(
			DragStartWorldPosition, HitAxis,
			Ray.WorldRay.Origin, Ray.WorldRay.Direction,
			bIntersects, IntersectionPoint);

		check(bIntersects);  // need to handle this case...

		InteractionStartPoint = IntersectionPoint;

		InteractionStartParameter = GizmoMath::ComputeAngleInPlane(InteractionStartPoint,
			DragStartWorldPosition, HitAxis, RotationPlaneX, RotationPlaneZ);
	}
}

void UDirectionalLightGizmo::OnUpdateDrag(const FInputDeviceRay& Ray)
{
	bool bIntersects; 
	FVector IntersectionPoint;

	// Calculate current hit parameters
	GizmoMath::RayPlaneIntersectionPoint(
		DragStartWorldPosition, HitAxis,
		Ray.WorldRay.Origin, Ray.WorldRay.Direction,
		bIntersects, IntersectionPoint);

	if (!bIntersects)
	{
		return;
	}

	FVector InteractionCurPoint = IntersectionPoint;

	float InteractionCurAngle = GizmoMath::ComputeAngleInPlane(InteractionCurPoint,
		DragStartWorldPosition, HitAxis, RotationPlaneX, RotationPlaneZ);

	float DeltaAngle = InteractionCurAngle - InteractionStartParameter;

	// Rotate around y axis if the arrow was hit
	if (HitComponent == GizmoActor->Arrow)
	{
		FRotator Rotation = FRotator::ZeroRotator;

		Rotation.Pitch = FMath::RadiansToDegrees(DeltaAngle);

		LightActor->AddActorLocalRotation(Rotation);
	}
	// Rotate around Z axis if the circle was hit
	else
	{
		FQuat Rotation(FVector(0, 0, 1), DeltaAngle);
		LightActor->AddActorWorldRotation(Rotation);
	}

	TransformProxy->SetTransform(LightActor->GetTransform());

	InteractionStartPoint = InteractionCurPoint;
	InteractionStartParameter = InteractionCurAngle;

}

bool UDirectionalLightGizmo::HitTest(const FRay& Ray, FHitResult& OutHit, FTransform& OutTransform, UPrimitiveComponent*& OutHitComponent)
{
	FVector Start = Ray.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.Origin + Ray.Direction * MaxRaycastDistance;

	FCollisionQueryParams Params;

	if (GizmoActor->Arrow->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutTransform = GizmoActor->Arrow->GetComponentTransform();
		OutHitComponent = GizmoActor->Arrow;
		return true;
	}
	else if (GizmoActor->RotationZCircle && GizmoActor->RotationZCircle->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutTransform = GizmoActor->GetTransform();
		OutHitComponent = GizmoActor->RotationZCircle;
		return true;
	}

	return false;
}

void UDirectionalLightGizmo::CreateGizmoHandles()
{
	FActorSpawnParameters SpawnInfo;
	GizmoActor = World->SpawnActor<ADirectionalLightGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	GizmoActor->Arrow = AGizmoActor::AddDefaultLineHandleComponent(World, GizmoActor, FLinearColor::Red, FVector::YAxisVector, FVector::XAxisVector, ArrowLength, true);
}

void UDirectionalLightGizmo::UpdateGizmoHandles()
{
	if (GizmoActor && GizmoActor->RotationZCircle)
	{
		GizmoActor->RotationZCircle->SetRelativeRotation(GizmoActor->GetActorRotation().Quaternion().Inverse());
	}
}

void UDirectionalLightGizmo::OnTransformChanged(UTransformProxy*, FTransform)
{
	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();

	FTransform TargetTransform = TransformProxy->GetTransform();

	FTransform GizmoTransform = TargetTransform;
	GizmoTransform.SetScale3D(FVector(1, 1, 1));

	GizmoComponent->SetWorldTransform(GizmoTransform);

	UpdateGizmoHandles();
}


void UDirectionalLightGizmo::CreateZRotationGizmo()
{
	UGizmoCircleComponent* NewCircle = NewObject<UGizmoCircleComponent>(GizmoActor);
	GizmoActor->AddInstanceComponent(NewCircle);
	NewCircle->AttachToComponent(GizmoActor->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	NewCircle->Normal = FVector::ZAxisVector;
	NewCircle->Color = FLinearColor::Blue;
	NewCircle->Radius = 120.f;
	NewCircle->RegisterComponent();

	GizmoActor->RotationZCircle = NewCircle;

	UpdateGizmoHandles();
}

// UDirectionalLightGizmoInputBehavior

void UDirectionalLightGizmoInputBehavior::Initialize(UDirectionalLightGizmo* InGizmo)
{
	Gizmo = InGizmo;
}

FInputCaptureRequest UDirectionalLightGizmoInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input))
	{
		FHitResult HitResult;
		FTransform DragTransform;
		UPrimitiveComponent* HitComponent = nullptr;
		if (Gizmo->HitTest(input.Mouse.WorldRay, HitResult, DragTransform, HitComponent))
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.Distance);
		}
	}

	return FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UDirectionalLightGizmoInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	Gizmo->OnBeginDrag(DeviceRay);
	bInputDragCaptured = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UDirectionalLightGizmoInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
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

void UDirectionalLightGizmoInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInputDragCaptured)
	{
		bInputDragCaptured = false;
	}
}