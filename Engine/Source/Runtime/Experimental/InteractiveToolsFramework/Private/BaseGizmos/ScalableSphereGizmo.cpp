// Copyright Epic Games, Inc. All Rights Reserved.

#include "BaseGizmos/ScalableSphereGizmo.h"
#include "InteractiveGizmoManager.h"
#include "SceneManagement.h"
#include "BaseGizmos/GizmoBoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/CollisionProfile.h"
#include "Kismet/KismetMathLibrary.h"
#include "BaseGizmos/GizmoMath.h"

// UScalableSphereGizmoBuilder

UInteractiveGizmo* UScalableSphereGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UScalableSphereGizmo* NewGizmo = NewObject<UScalableSphereGizmo>(SceneState.GizmoManager);

	// Have to set world to be able to spawn actor
	NewGizmo->SetWorld(SceneState.World);
	
	return NewGizmo;
}

// AScalableSphereGizmoActor

AScalableSphereGizmoActor::AScalableSphereGizmoActor()
{
	// root component is a hidden sphere
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("GizmoCenter"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

// UScalableSphereGizmo

void UScalableSphereGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	Radius = 1000.0f;

	UScalableSphereGizmoInputBehavior* ScalableSphereBehavior = NewObject<UScalableSphereGizmoInputBehavior>(this);
	ScalableSphereBehavior->Initialize(this);
	AddInputBehavior(ScalableSphereBehavior);

	CreateGizmoHandles();
}

void UScalableSphereGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ActiveTarget)
	{
		DrawWireSphereAutoSides(RenderAPI->GetPrimitiveDrawInterface(), ActiveTarget->GetTransform(), FColor(200, 255, 255), Radius, SDPG_World);
	}
}

void UScalableSphereGizmo::Shutdown()
{
	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
}

void UScalableSphereGizmo::SetTarget(UTransformProxy* InTarget)
{
	ActiveTarget = InTarget;

	// Make sure the internal GizmoActor updates when the transform of the target is changed
	ActiveTarget->OnTransformChanged.AddUObject(this, &UScalableSphereGizmo::OnTransformChanged);
	
	OnTransformChanged(InTarget, InTarget->GetTransform());
}

void UScalableSphereGizmo::SetWorld(UWorld* InWorld)
{
	World = InWorld;

}

void UScalableSphereGizmo::CreateGizmoHandles()
{
	FActorSpawnParameters SpawnInfo;
	GizmoActor = World->SpawnActor<AScalableSphereGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	// Create all the 6 handles in each direction

	auto NewHandle = [this] { return AGizmoActor::AddDefaultBoxComponent(World, GizmoActor, FLinearColor(1.0f, 0.0f, 0.0f), FVector::ZeroVector); };
	GizmoActor->XPositive = NewHandle();
	GizmoActor->XNegative = NewHandle();
	GizmoActor->YPositive = NewHandle();
	GizmoActor->YNegative = NewHandle();
	GizmoActor->ZPositive = NewHandle();
	GizmoActor->ZNegative = NewHandle();

	UpdateGizmoHandles();

}

void UScalableSphereGizmo::UpdateGizmoHandles()
{
	GizmoActor->XPositive->SetRelativeLocation(FVector::XAxisVector * Radius);
	GizmoActor->XNegative->SetRelativeLocation(-FVector::XAxisVector * Radius);

	GizmoActor->YPositive->SetRelativeLocation(FVector::YAxisVector * Radius);
	GizmoActor->YNegative->SetRelativeLocation(-FVector::YAxisVector * Radius);

	GizmoActor->ZPositive->SetRelativeLocation(FVector::ZAxisVector * Radius);
	GizmoActor->ZNegative->SetRelativeLocation(-FVector::ZAxisVector * Radius);
}

void UScalableSphereGizmo::OnTransformChanged(UTransformProxy*, FTransform)
{
	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();

	FTransform TargetTransform = ActiveTarget->GetTransform();

	// Don't scale the internal gizmo actor (only update position and rotation changes)
	TargetTransform.SetScale3D(FVector(1, 1, 1));

	GizmoComponent->SetWorldTransform(TargetTransform);
}

bool UScalableSphereGizmo::HitTest(const FRay& Ray, FHitResult& OutHit, FVector& OutAxis, FTransform &OutTransform)
{
	if (!ActiveTarget)
	{
		return false;
	}

	FVector Start = Ray.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.Origin + Ray.Direction * MaxRaycastDistance;

	FCollisionQueryParams Params;

	// Check each actor to see if any of them were hit
	if (GizmoActor->XPositive->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = FVector::XAxisVector;
		OutTransform = GizmoActor->XPositive->GetComponentTransform();
		return true;
	}
	if (GizmoActor->XNegative->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = -FVector::XAxisVector;
		OutTransform = GizmoActor->XNegative->GetComponentTransform();
		return true;
	}

	if (GizmoActor->YPositive->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = FVector::YAxisVector;
		OutTransform = GizmoActor->YPositive->GetComponentTransform();
		return true;
	}
	if (GizmoActor->YNegative->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = -FVector::YAxisVector;
		OutTransform = GizmoActor->YNegative->GetComponentTransform();
		return true;
	}

	if (GizmoActor->ZPositive->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = FVector::ZAxisVector;
		OutTransform = GizmoActor->ZPositive->GetComponentTransform();
		return true;
	}
	if (GizmoActor->ZNegative->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = -FVector::ZAxisVector;
		OutTransform = GizmoActor->ZNegative->GetComponentTransform();
		return true;
	}

	return false;
}

void UScalableSphereGizmo::SetRadius(float InRadius)
{
	// Negative Radius not allowed
	if (InRadius < 0)
	{
		InRadius = 0;
	}

	Radius = InRadius;

	if (UpdateRadiusFunc)
	{
		UpdateRadiusFunc(Radius);
	}

	UpdateGizmoHandles();
}

void UScalableSphereGizmo::OnBeginDrag(const FInputDeviceRay& Ray)
{
	FVector Start = Ray.WorldRay.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.WorldRay.Origin + Ray.WorldRay.Direction * MaxRaycastDistance;

	FRay HitCheckRay(Start, End - Start);
	FHitResult HitResult;
	FVector HitAxis;
	FTransform DragTransform;

	 // Check if the Ray hit any of the handles
	if (HitTest(HitCheckRay, HitResult, HitAxis, DragTransform))
	{
		ActiveAxis = HitAxis;

		FVector RayNearestPt; 
		float RayNearestParam;
		FVector InteractionStartPoint;

		 // Find the initial parameters along the hit axis
		GizmoMath::NearestPointOnLineToRay(DragTransform.GetLocation(), ActiveAxis,
			Ray.WorldRay.Origin, Ray.WorldRay.Direction,
			InteractionStartPoint, InteractionStartParameter,
			RayNearestPt, RayNearestParam);

		DragStartWorldPosition = DragTransform.GetLocation();
	}
}

void UScalableSphereGizmo::OnUpdateDrag(const FInputDeviceRay& Ray)
{
	FVector AxisNearestPt; 
	float AxisNearestParam;
	FVector RayNearestPt; 
	float RayNearestParam;

	 // Find the parameters along the hit axis
	GizmoMath::NearestPointOnLineToRay(DragStartWorldPosition, ActiveAxis,
		Ray.WorldRay.Origin, Ray.WorldRay.Direction,
		AxisNearestPt, AxisNearestParam,
		RayNearestPt, RayNearestParam);

	float InteractionCurrentParameter = AxisNearestParam;

	float DeltaParam = InteractionCurrentParameter - InteractionStartParameter;

	InteractionStartParameter = InteractionCurrentParameter;

	 // Update the radius
	SetRadius(Radius + DeltaParam);
}

// UScalableSphereGizmoInputBehavior

void UScalableSphereGizmoInputBehavior::Initialize(UScalableSphereGizmo* InGizmo)
{
	Gizmo = InGizmo;
}

FInputCaptureRequest UScalableSphereGizmoInputBehavior::WantsCapture(const FInputDeviceState& input)
{
	if (IsPressed(input))
	{
		FHitResult HitResult;
		FVector HitAxis;
		FTransform DragTransform;
		if (Gizmo->HitTest(input.Mouse.WorldRay, HitResult, HitAxis, DragTransform))
		{
			return FInputCaptureRequest::Begin(this, EInputCaptureSide::Any, HitResult.Distance);
		}
	}

	return FInputCaptureRequest::Ignore();
}

FInputCaptureUpdate UScalableSphereGizmoInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	// Forward behavior to the Gizmo
	Gizmo->OnBeginDrag(DeviceRay);

	bInputDragCaptured = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UScalableSphereGizmoInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	if (IsReleased(input))
	{
		bInputDragCaptured = false;
		return FInputCaptureUpdate::End();
	}

	// Forward behavior to the Gizmo
	Gizmo->OnUpdateDrag(LastWorldRay);

	return FInputCaptureUpdate::Continue();
}

void UScalableSphereGizmoInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInputDragCaptured)
	{
		bInputDragCaptured = false;
	}
}