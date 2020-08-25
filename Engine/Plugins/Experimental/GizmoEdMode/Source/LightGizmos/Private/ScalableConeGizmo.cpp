// Copyright Epic Games, Inc. All Rights Reserved.

#include "ScalableConeGizmo.h"
#include "Components/SphereComponent.h"
#include "BaseGizmos/GizmoBoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "BaseGizmos/GizmoMath.h"

// UScalableConeGizmoBuilder

UInteractiveGizmo* UScalableConeGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UScalableConeGizmo* NewGizmo = NewObject<UScalableConeGizmo>(SceneState.GizmoManager);
	NewGizmo->SetWorld(SceneState.World);
	return NewGizmo;
}

// AScalableConeGizmoActor

AScalableConeGizmoActor::AScalableConeGizmoActor()
{
	// root component is a hidden sphere
	USphereComponent* SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("GizmoCenter"));
	RootComponent = SphereComponent;
	SphereComponent->InitSphereRadius(1.0f);
	SphereComponent->SetVisibility(false);
	SphereComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}

// UScalableConeGizmo

void UScalableConeGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	Length = 1000.0f;
	Angle = 45.f;
	MaxAngle = 90.f;
	MinAngle = 0.f;
	ConeColor = FColor(200, 255, 255);

	UScalableConeGizmoInputBehavior* ScalableConeBehavior = NewObject<UScalableConeGizmoInputBehavior>(this);
	ScalableConeBehavior->Initialize(this);
	AddInputBehavior(ScalableConeBehavior);

	CreateGizmoHandles();
}

void UScalableConeGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ActiveTarget)
	{
		DrawWireSphereCappedCone(RenderAPI->GetPrimitiveDrawInterface(), GizmoActor->GetTransform(), Length, Angle, 32, 8, 10, ConeColor, SDPG_World);
	}
}

void UScalableConeGizmo::Shutdown()
{
	if (GizmoActor)
	{
		GizmoActor->Destroy();
		GizmoActor = nullptr;
	}
}

void UScalableConeGizmo::SetTarget(UTransformProxy* InTarget)
{
	ActiveTarget = InTarget;

	// To update the internal GizmoActor when the transform is changed
	ActiveTarget->OnTransformChanged.AddUObject(this, &UScalableConeGizmo::OnTransformChanged);

	OnTransformChanged(InTarget, InTarget->GetTransform());

}

void UScalableConeGizmo::SetWorld(UWorld* InWorld)
{
	World = InWorld;
}

void UScalableConeGizmo::SetAngleDegrees(float InAngle)
{
	Angle = InAngle;

	Angle = FMath::Clamp<float>(Angle, MinAngle, MaxAngle);

	UpdateGizmoHandles();

	if (UpdateAngleFunc)
	{
		UpdateAngleFunc(InAngle);
	}
}

void UScalableConeGizmo::SetLength(float InLength)
{
	Length = InLength;
}

float UScalableConeGizmo::GetLength()
{
	return Length;
}

float UScalableConeGizmo::GetAngleDegrees()
{
	return Angle;
}

void UScalableConeGizmo::CreateGizmoHandles()
{
	FActorSpawnParameters SpawnInfo;
	GizmoActor = World->SpawnActor<AScalableConeGizmoActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnInfo);

	 // Handles are Boxes right now
	GizmoActor->ScaleHandleYPlus = AGizmoActor::AddDefaultBoxComponent(World, GizmoActor, FLinearColor(1.0, 0, 0), FVector::ZeroVector);
	GizmoActor->ScaleHandleYMinus = AGizmoActor::AddDefaultBoxComponent(World, GizmoActor, FLinearColor(1.0, 0, 0), FVector::ZeroVector);
	GizmoActor->ScaleHandleZPlus = AGizmoActor::AddDefaultBoxComponent(World, GizmoActor, FLinearColor(1.0, 0, 0), FVector::ZeroVector);
	GizmoActor->ScaleHandleZMinus = AGizmoActor::AddDefaultBoxComponent(World, GizmoActor, FLinearColor(1.0, 0, 0), FVector::ZeroVector);
	
	UpdateGizmoHandles();
}

void UScalableConeGizmo::UpdateGizmoHandles()
{
	 // Get radius and height of the cone
	float Radius = Length * FMath::Sin(FMath::DegreesToRadians(Angle));
	float Height = Length * FMath::Cos(FMath::DegreesToRadians(Angle));

	GizmoActor->ScaleHandleYPlus->SetRelativeLocation(FVector::XAxisVector * Height + FVector::YAxisVector * Radius);
	GizmoActor->ScaleHandleYMinus->SetRelativeLocation(FVector::XAxisVector * Height - FVector::YAxisVector * Radius);
	GizmoActor->ScaleHandleZPlus->SetRelativeLocation(FVector::XAxisVector * Height + FVector::ZAxisVector * Radius);
	GizmoActor->ScaleHandleZMinus->SetRelativeLocation(FVector::XAxisVector * Height - FVector::ZAxisVector * Radius);
}

void UScalableConeGizmo::OnTransformChanged(UTransformProxy*, FTransform)
{
	USceneComponent* GizmoComponent = GizmoActor->GetRootComponent();

	FTransform TargetTransform = ActiveTarget->GetTransform();

	// GizmoActor doesn't want the scale of the object
	TargetTransform.SetScale3D(FVector(1, 1, 1));

	GizmoComponent->SetWorldTransform(TargetTransform);

	UpdateGizmoHandles();
}

bool UScalableConeGizmo::HitTest(const FRay& Ray, FHitResult& OutHit, FVector& OutAxis, FTransform& OutTransform)
{
	if (!ActiveTarget)
	{
		return false;
	}

	FVector Start = Ray.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.Origin + Ray.Direction * MaxRaycastDistance;

	FCollisionQueryParams Params;

	FQuat Rotation = ActiveTarget->GetTransform().GetRotation();

	// Check which component was hit and update OutAxis and OutTransform
	if (GizmoActor->ScaleHandleYPlus->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = Rotation.RotateVector(FVector::YAxisVector);
		OutTransform = GizmoActor->ScaleHandleYPlus->GetComponentTransform();
		return true;
	}
	if (GizmoActor->ScaleHandleYMinus->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = Rotation.RotateVector(-FVector::YAxisVector);
		OutTransform = GizmoActor->ScaleHandleYMinus->GetComponentTransform();
		return true;
	}
	if (GizmoActor->ScaleHandleZPlus->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = Rotation.RotateVector(FVector::ZAxisVector);
		OutTransform = GizmoActor->ScaleHandleZPlus->GetComponentTransform();
		return true;
	}
	if (GizmoActor->ScaleHandleZMinus->LineTraceComponent(OutHit, Start, End, Params))
	{
		OutAxis = Rotation.RotateVector(-FVector::ZAxisVector);
		OutTransform = GizmoActor->ScaleHandleZMinus->GetComponentTransform();
		return true;
	}

	return false;

}

void UScalableConeGizmo::OnBeginDrag(const FInputDeviceRay& Ray)
{
	FVector Start = Ray.WorldRay.Origin;
	const float MaxRaycastDistance = 1e6f;
	FVector End = Ray.WorldRay.Origin + Ray.WorldRay.Direction * MaxRaycastDistance;

	FRay HitCheckRay(Start, End - Start);
	FHitResult HitResult;
	FTransform DragTransform;

	 // Check if any component was hit
	if (HitTest(HitCheckRay, HitResult, HitAxis, DragTransform))
	{
		FVector RayNearestPt; float RayNearestParam;

		// Get the initial interaction parameters
		GizmoMath::NearestPointOnLineToRay(DragTransform.GetLocation(), HitAxis,
			Ray.WorldRay.Origin, Ray.WorldRay.Direction,
			InteractionStartPoint, InteractionStartParameter,
			RayNearestPt, RayNearestParam);

		DragStartWorldPosition = DragTransform.GetLocation();
	}
}

void UScalableConeGizmo::OnUpdateDrag(const FInputDeviceRay& Ray)
{
	FVector AxisNearestPt; float AxisNearestParam;
	FVector RayNearestPt; float RayNearestParam;

	// Get the current interaction parameters
	GizmoMath::NearestPointOnLineToRay(DragStartWorldPosition, HitAxis,
		Ray.WorldRay.Origin, Ray.WorldRay.Direction,
		AxisNearestPt, AxisNearestParam,
		RayNearestPt, RayNearestParam);

	FVector GizmoLocation = GizmoActor->GetActorLocation();

	// Vector to the starting position of the interaction
	FVector Start = InteractionStartPoint - GizmoLocation;
	Start.Normalize();

	// Vector to the ending position of the interaction
	FVector End = AxisNearestPt - GizmoLocation;
	End.Normalize();

	float DotP = FVector::DotProduct(Start, End);

	float DeltaAngle = FMath::Acos(DotP);

	// Get the angle between the start and end vectors and the forward vector to check if the drag direction should be +ve or -ve
	float StartAngle = FMath::Acos(FVector::DotProduct(Start, GizmoActor->GetActorForwardVector()));
	float EndAngle = FMath::Acos(FVector::DotProduct(End, GizmoActor->GetActorForwardVector()));

	if (StartAngle > EndAngle)
	{
		DeltaAngle = -DeltaAngle;
	}

	SetAngleDegrees(Angle + FMath::RadiansToDegrees(DeltaAngle));

	InteractionStartPoint = AxisNearestPt;
	InteractionStartParameter = AxisNearestParam;
}

// UScalableConeGizmoInputBehavior

void UScalableConeGizmoInputBehavior::Initialize(UScalableConeGizmo* InGizmo)
{
	Gizmo = InGizmo;
}

FInputCaptureRequest UScalableConeGizmoInputBehavior::WantsCapture(const FInputDeviceState& input)
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

FInputCaptureUpdate UScalableConeGizmoInputBehavior::BeginCapture(const FInputDeviceState& input, EInputCaptureSide eSide)
{
	FInputDeviceRay DeviceRay(input.Mouse.WorldRay, input.Mouse.Position2D);
	LastWorldRay = DeviceRay.WorldRay;
	LastScreenPosition = DeviceRay.ScreenPosition;

	Gizmo->OnBeginDrag(DeviceRay);
	bInputDragCaptured = true;
	return FInputCaptureUpdate::Begin(this, EInputCaptureSide::Any);
}

FInputCaptureUpdate UScalableConeGizmoInputBehavior::UpdateCapture(const FInputDeviceState& input, const FInputCaptureData& data)
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

void UScalableConeGizmoInputBehavior::ForceEndCapture(const FInputCaptureData& data)
{
	if (bInputDragCaptured)
	{
		bInputDragCaptured = false;
	}
}