// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapMovementComponent.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapMath.h"
#include "IMagicLeapMovementPlugin.h"
#include "Components/PrimitiveComponent.h"

DEFINE_LOG_CATEGORY(LogMagicLeapMovement);

class FMagicLeapMovementPlugin : public IMagicLeapMovementPlugin
{};

IMPLEMENT_MODULE(FMagicLeapMovementPlugin, MagicLeapMovement);

#if WITH_MLSDK
void MLToUEMovementSettings(const MLMovementSettings& InMLMovementSettings, FMagicLeapMovementSettings& OutUE4MovementSettings)
{
	const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();

	OutUE4MovementSettings.SwayHistorySize = InMLMovementSettings.sway_history_size;
	OutUE4MovementSettings.MaxDeltaAngle = FMath::RadiansToDegrees(InMLMovementSettings.max_delta_angle);
	OutUE4MovementSettings.ControlDampeningFactor = InMLMovementSettings.control_dampening_factor;
	OutUE4MovementSettings.MaxSwayAngle = FMath::RadiansToDegrees(InMLMovementSettings.max_sway_angle);
	OutUE4MovementSettings.MaximumHeadposeRotationSpeed = FMath::RadiansToDegrees(InMLMovementSettings.maximum_headpose_rotation_speed);
	OutUE4MovementSettings.MaximumSwayTimeS = InMLMovementSettings.maximum_sway_time_s;
	OutUE4MovementSettings.EndResolveTimeoutS = InMLMovementSettings.end_resolve_timeout_s;

	OutUE4MovementSettings.MaximumHeadposeMovementSpeed = InMLMovementSettings.maximum_headpose_movement_speed * WorldToMetersScale;
	OutUE4MovementSettings.MaximumDepthDeltaForSway = InMLMovementSettings.maximum_depth_delta_for_sway * WorldToMetersScale;
	OutUE4MovementSettings.MinimumDistance = InMLMovementSettings.minimum_distance * WorldToMetersScale;
	OutUE4MovementSettings.MaximumDistance = InMLMovementSettings.maximum_distance * WorldToMetersScale;
}

void UEToMLMovementSettings(const FMagicLeapMovementSettings& InUE4MovementSettings, MLMovementSettings& OutMLMovementSettings)
{
	static constexpr int32 LowerSwayHistorySize = 3;
	static constexpr int32 UpperSwayHistorySize = 100;
	static constexpr float LowerMaxDeltaAngle = 1.0f;
	static constexpr float UpperMaxDeltaAngle = 360.0f;
	static constexpr float LowerControlDampeningFactor = 0.5f;
	static constexpr float LowerMaxSwayAngle = 0.0f;
	static constexpr float UpperMaxSwayAngle = 360.0f;
	static constexpr float LowerMaxHeadposeRotationSpeed = 0.0f;
	static constexpr float UpperMaxHeadposeRotationSpeed = 360.0f;
	static constexpr float LowerMaxSwayTime = 0.15f;

	static constexpr float LowerMaxHeadposeMovementSpeed = 75.0f;
	static constexpr float LowerMaxDepthDeltaForSway = 10.0f;
	static constexpr float LowerMinDistance = 50.0f;
	static constexpr float DistanceMinMaxLowerDelta = 10.0f;

	const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
	MLMovementSettingsInit(&OutMLMovementSettings);
	OutMLMovementSettings.sway_history_size = FMath::Clamp(InUE4MovementSettings.SwayHistorySize, LowerSwayHistorySize, UpperSwayHistorySize);
	OutMLMovementSettings.max_delta_angle = FMath::DegreesToRadians(FMath::Clamp(InUE4MovementSettings.MaxDeltaAngle, LowerMaxDeltaAngle, UpperMaxDeltaAngle));
	OutMLMovementSettings.control_dampening_factor = FMath::Max(LowerControlDampeningFactor, InUE4MovementSettings.ControlDampeningFactor);
	OutMLMovementSettings.max_sway_angle = FMath::DegreesToRadians(FMath::Clamp(InUE4MovementSettings.MaxSwayAngle, LowerMaxSwayAngle, UpperMaxSwayAngle));
	OutMLMovementSettings.maximum_headpose_rotation_speed = FMath::DegreesToRadians(FMath::Clamp(InUE4MovementSettings.MaximumHeadposeRotationSpeed, LowerMaxHeadposeRotationSpeed, UpperMaxHeadposeRotationSpeed));
	OutMLMovementSettings.maximum_sway_time_s = FMath::Max(LowerMaxSwayTime, InUE4MovementSettings.MaximumSwayTimeS);
	OutMLMovementSettings.end_resolve_timeout_s = InUE4MovementSettings.EndResolveTimeoutS;

	OutMLMovementSettings.maximum_headpose_movement_speed = FMath::Max(LowerMaxHeadposeMovementSpeed, InUE4MovementSettings.MaximumHeadposeMovementSpeed) / WorldToMetersScale;
	OutMLMovementSettings.maximum_depth_delta_for_sway = FMath::Max(LowerMaxDepthDeltaForSway, InUE4MovementSettings.MaximumDepthDeltaForSway) / WorldToMetersScale;
	OutMLMovementSettings.minimum_distance = FMath::Clamp(InUE4MovementSettings.MinimumDistance, LowerMinDistance, InUE4MovementSettings.MaximumDistance) / WorldToMetersScale;
	OutMLMovementSettings.maximum_distance = FMath::Max(InUE4MovementSettings.MinimumDistance + DistanceMinMaxLowerDelta, InUE4MovementSettings.MaximumDistance) / WorldToMetersScale;
}


void UEToMLMovement3DofSettings(const FMagicLeapMovement3DofSettings& InUEMovement3DofSettings, MLMovement3DofSettings& OutMLMovement3DofSettings)
{
	MLMovement3DofSettingsInit(&OutMLMovement3DofSettings);
	OutMLMovement3DofSettings.auto_center = InUEMovement3DofSettings.bAutoCenter;
}

void UEToMLMovement6DofSettings(const FMagicLeapMovement6DofSettings& InUEMovement6DofSettings, MLMovement6DofSettings& OutMLMovement6DofSettings)
{
	MLMovement6DofSettingsInit(&OutMLMovement6DofSettings);
	OutMLMovement6DofSettings.auto_center = InUEMovement6DofSettings.bAutoCenter;
}

#endif // WITH_MLSDK

UMagicLeapMovementComponent::UMagicLeapMovementComponent()
: bSlideAlongSurfaceOnBlockingHit(true)
, bSoftCollideWithOverlappingActors(false)
, MovementController(nullptr)
, CurrentMovementType(EMagicLeapMovementType::Controller6DOF)
, bSoftCollisionsPending(false)
#if WITH_MLSDK
, SessionHandle(ML_INVALID_HANDLE)
, HardCollisionHandle(ML_INVALID_HANDLE)
#endif // WITH_MLSDK
{}

void UMagicLeapMovementComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_MLSDK
	// Update mvement only if both the Update and the Controller components are selected and we are not waiting for any pending soft collisions to resolve.
	if (UpdatedComponent != nullptr && MovementController != nullptr && !bSoftCollisionsPending)
	{
		// Start out with the current transform of the Update component.
		MLMovementObject MovementObject;
		GetMovementTransform(MovementObject);

		// Check for change in Dof mode.
		if (MovementSettings.MovementType != CurrentMovementType)
		{
			// If left in an overlapped state with anther component, the new mode would not resolve it since the
			// soft collisions map would be empty. Hence we keep a copy of the overlapping components and
			// re-generate the soft collisions for the new session.
			TArray<UPrimitiveComponent*> ExistingSoftCollisions;
			SoftCollisions.GetKeys(ExistingSoftCollisions);

			// If mode was changed, end current movement session without waiting for pending collisions to resolve.
			// Any pending collisions will be resolved by the new session.
			EndMovementSession(false, DeltaTime);
			CurrentMovementType = MovementSettings.MovementType;

			// Will re-generate a new handle for the existing soft collisions.
			for (UPrimitiveComponent* OtherComp : ExistingSoftCollisions)
			{
				SoftCollisions.Add(OtherComp, ML_INVALID_HANDLE);
			}
		}

		// Get recommended transform from the platform library
		if (CurrentMovementType == EMagicLeapMovementType::Controller3DOF)
		{
			Update3DofMovement(DeltaTime, MovementObject);
		}
		else if (CurrentMovementType == EMagicLeapMovementType::Controller6DOF)
		{
			Update6DofMovement(DeltaTime, MovementObject);
		}

		// Set transform of Update component and register hard collisions.
		SetMovementTransform(MovementObject, true);

		if (bSoftCollideWithOverlappingActors)
		{
			// Soft collisions are currently supported only with static components.

			// Stop any existing soft collisions who's bounding volumes are no longer overlapping.
			for (MLHandle Handle : SoftCollisionsPendingDestroy)
			{
				EndCollision(Handle);
			}
			SoftCollisionsPendingDestroy.Empty();

			const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
			const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);

			for (auto& SoftCollisionPair : SoftCollisions)
			{
				// If this soft collision hasnt been started yet
				if (!MLHandleIsValid(SoftCollisionPair.Value))
				{
					const FBoxSphereBounds& OverlappedBounds = SoftCollisionPair.Key->Bounds;
					const MLVec3f OtherPos = MagicLeap::ToMLVector(TrackingToWorld.Inverse().TransformPosition(OverlappedBounds.Origin), WorldToMetersScale);
					const float MinDepth = (UpdatedComponent->Bounds.SphereRadius * (1.0f - MovementSettings.MaxPenetrationPercentage)) / WorldToMetersScale;
					const float MaxDepth = (OverlappedBounds.SphereRadius + UpdatedComponent->Bounds.SphereRadius) / WorldToMetersScale;

					MLResult Result = MLMovementStartSoftCollision(SessionHandle, &OtherPos, MinDepth, MaxDepth, &SoftCollisionPair.Value);
					UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementStartSoftCollision failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
				}
			}
		}
	}
	else
	{
		EndMovementSession(true, DeltaTime);
	}	
#endif // WITH_MLSDK
}

void UMagicLeapMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Abruptly stop all collisions and end movement session on EndPlay
	EndMovementSession(false, 0.016f);
	Super::EndPlay(EndPlayReason);
}

void UMagicLeapMovementComponent::AttachObjectToMovementController(const USceneComponent* InMovementController)
{
	MovementController = InMovementController;
}

void UMagicLeapMovementComponent::DetachObject(bool bResolvePendingSoftCollisions)
{
	MovementController = nullptr;
	// The movement controller has been detached so we will no longer be updating the position of the component
	// unless the user chooses to resolve any pending soft collisions.
	if (!bResolvePendingSoftCollisions)
	{
#if WITH_MLSDK
		EndAllSoftCollisions();
#endif // WITH_MLSDK
	}
}

bool UMagicLeapMovementComponent::GetDefaultSettings(FMagicLeapMovementSettings& OutSettings) const
{
#if WITH_MLSDK
	MLMovementSettings DefaultSettings;
	MLMovementSettingsInit(&DefaultSettings);
	MLResult Result = MLMovementGetDefaultSettings(&DefaultSettings);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMovement, Error, TEXT("MLMovementGetDefaultSettings failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
		return false;
	}

	MLToUEMovementSettings(DefaultSettings, OutSettings);
	OutSettings.MovementType = EMagicLeapMovementType::Controller6DOF;
	OutSettings.MaxPenetrationPercentage = 0.3f;

	return true;
#else
	return false;
#endif // WITH_MLSDK
}

bool UMagicLeapMovementComponent::ChangeDepth(float DeltaDepth)
{
#if WITH_MLSDK
	MLResult Result = MLMovementChangeDepth(SessionHandle, DeltaDepth / IMagicLeapPlugin::Get().GetWorldToMetersScale());
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMovement, Error, TEXT("MLMovementChangeDepth failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
		return false;
	}

	return true;
#else
	return false;
#endif // WITH_MLSDK
}

bool UMagicLeapMovementComponent::ChangeRotation(float DeltaDegrees)
{
#if WITH_MLSDK
	MLResult Result = MLMovementChangeRotation(SessionHandle, FMath::DegreesToRadians(DeltaDegrees));
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeapMovement, Error, TEXT("MLMovementChangeRotation failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
		return false;
	}

	return true;
#else
	return false;
#endif // WITH_MLSDK
}

void UMagicLeapMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
	if (UpdatedPrimitive != nullptr)
	{
		UpdatedPrimitive->OnComponentBeginOverlap.RemoveAll(this);
		UpdatedPrimitive->OnComponentEndOverlap.RemoveAll(this);
	}

	Super::SetUpdatedComponent(NewUpdatedComponent);

	if (UpdatedPrimitive != nullptr && !UpdatedPrimitive->IsPendingKill())
	{
		UpdatedPrimitive->OnComponentBeginOverlap.AddDynamic(this, &UMagicLeapMovementComponent::OnUpdatedComponentOverlapBegin);
		UpdatedPrimitive->OnComponentEndOverlap.AddDynamic(this, &UMagicLeapMovementComponent::OnUpdatedComponentOverlapEnd);
	}
}

void UMagicLeapMovementComponent::EndMovementSession(bool bResolvePendingSoftCollisions, float DeltaTime)
{
#if WITH_MLSDK
	if (MLHandleIsValid(SessionHandle))
	{
		EndCollision(HardCollisionHandle);

		bSoftCollisionsPending = false;
		// Stop any existing soft collisions who's bounding volumes are no longer overlapping.
		for (MLHandle Handle : SoftCollisionsPendingDestroy)
		{
			EndCollision(Handle);
		}
		SoftCollisionsPendingDestroy.Empty();

		// No component to update the transform for, so no point waiting for soft collisions to resolve.
		if (UpdatedComponent == nullptr || !bResolvePendingSoftCollisions)
		{
			EndAllSoftCollisions();
		}

		MLMovementObject MovementObject;
		GetMovementTransform(MovementObject);

		MLResult EndResult = MLMovementEnd(SessionHandle, DeltaTime, &MovementObject);
		switch (EndResult)
		{
			case MLResult_Ok:
			case MLResult_Timeout:
			{
				// Update transform one last time but ignore hard collision registration since we are ending the session.
				SetMovementTransform(MovementObject, false);
				SessionHandle = ML_INVALID_HANDLE;
				break;
			}
			case MLResult_Pending:
			{
				bSoftCollisionsPending = true;
				// Update transform for soft collision resolution but ignore hard collision registration since we are ending the session.
				SetMovementTransform(MovementObject, false);
				break;
			}
			default:
			{
				SessionHandle = ML_INVALID_HANDLE;
				UE_LOG(LogMagicLeapMovement, Error, TEXT("MLMovementEnd failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(EndResult)));
				break;
			}
		}
	}
#endif // WITH_MLSDK
}

void UMagicLeapMovementComponent::OnUpdatedComponentOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bSoftCollideWithOverlappingActors)
	{
#if WITH_MLSDK
		if (!SoftCollisions.Contains(OtherComp))
		{
			SoftCollisions.Add(OtherComp, ML_INVALID_HANDLE);
		}
#endif // WITH_MLSDK
	}
}

void UMagicLeapMovementComponent::OnUpdatedComponentOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* Other, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (bSoftCollideWithOverlappingActors)
	{
#if WITH_MLSDK
		MLHandle Handle = ML_INVALID_HANDLE;
		SoftCollisions.RemoveAndCopyValue(OtherComp, Handle);
		if (MLHandleIsValid(Handle))
		{
			SoftCollisionsPendingDestroy.Add(Handle);
		}
#endif // WITH_MLSDK
	}
}

#if WITH_MLSDK
void UMagicLeapMovementComponent::Update3DofMovement(float DeltaTime, MLMovementObject& MovementObject)
{
	MLMovement3DofControls Movement3DofControls;
	GetMovement3DofControls(Movement3DofControls);

	if (MLHandleIsValid(SessionHandle))
	{
		MLResult Result = MLMovementUpdate3Dof(SessionHandle, &Movement3DofControls, DeltaTime, &MovementObject);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementUpdate3Dof failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
	}
	else
	{
		MLMovementSettings Settings;
		UEToMLMovementSettings(MovementSettings, Settings);

		MLMovement3DofSettings Movement3DofSettings;
		UEToMLMovement3DofSettings(Settings3Dof, Movement3DofSettings);

		MLResult Result = MLMovementStart3Dof(&Settings, &Movement3DofSettings, &Movement3DofControls, &MovementObject, &SessionHandle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementStart3Dof failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));

		CurrentMovementType = EMagicLeapMovementType::Controller3DOF;
	}
}

void UMagicLeapMovementComponent::Update6DofMovement(float DeltaTime, MLMovementObject& MovementObject)
{
	MLMovement6DofControls Movement6DofControls;
	GetMovement6DofControls(Movement6DofControls);

	if (MLHandleIsValid(SessionHandle))
	{
		MLResult Result = MLMovementUpdate6Dof(SessionHandle, &Movement6DofControls, DeltaTime, &MovementObject);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementUpdate6Dof failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
	}
	else
	{
		MLMovementSettings Settings;
		UEToMLMovementSettings(MovementSettings, Settings);

		MLMovement6DofSettings Movement6DofSettings;
		UEToMLMovement6DofSettings(Settings6Dof, Movement6DofSettings);

		MLResult Result = MLMovementStart6Dof(&Settings, &Movement6DofSettings, &Movement6DofControls, &MovementObject, &SessionHandle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementStart6Dof failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));

		CurrentMovementType = EMagicLeapMovementType::Controller6DOF;
	}
}

void UMagicLeapMovementComponent::UpdateHardCollision(const FVector& ImpactNormal)
{
	const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
	const MLVec3f ContactNormal = MagicLeap::ToMLVectorNoScale(WorldToTracking.TransformVectorNoScale(ImpactNormal)); 

	if (MLHandleIsValid(HardCollisionHandle))
	{
		MLResult Result = MLMovementUpdateHardCollision(SessionHandle, HardCollisionHandle, &ContactNormal);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementUpdateHardCollision failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
	}
	else
	{
		MLResult Result = MLMovementStartHardCollision(SessionHandle, &ContactNormal, &HardCollisionHandle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementStartHardCollision failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
	}
}

void UMagicLeapMovementComponent::SetMovementTransform(const MLMovementObject& MovementObject, bool bRegisterHardCollision)
{
	if (UpdatedComponent != nullptr)
	{
		const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
		const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
		FTransform ComponentTransform = FTransform(MagicLeap::ToFQuat(MovementObject.object_rotation), MagicLeap::ToFVector(MovementObject.object_position, WorldToMetersScale), FVector(1.0f, 1.0f, 1.0f));
		ComponentTransform = ComponentTransform * TrackingToWorld;

		const FVector CurrLocation = UpdatedComponent->GetComponentLocation();
		const FVector MoveDelta = ComponentTransform.GetLocation() - CurrLocation;
		FHitResult Hit;
		// Try to move the Update component to the recommended location.
		SafeMoveUpdatedComponent(MoveDelta, ComponentTransform.GetRotation(), true, Hit);
		// If a blocking collisions was found during that attempted move, register it with the library as a hard collision.
		if (Hit.IsValidBlockingHit())
		{
			if (bRegisterHardCollision)
			{
				UpdateHardCollision(Hit.ImpactNormal);
			}

			if (bSlideAlongSurfaceOnBlockingHit)
			{
				SlideAlongSurface(MoveDelta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
			}
		}
		else
		{
			// Deregister hard collision if none is found.
			EndCollision(HardCollisionHandle);
		}
	}
}

void UMagicLeapMovementComponent::GetMovementTransform(MLMovementObject& MovementObject) const
{
	MLMovementObjectInit(&MovementObject);
	if (UpdatedComponent != nullptr)
	{
		const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
		const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();
		const FTransform ComponentTransform = UpdatedComponent->GetComponentToWorld();

		MovementObject.object_position = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(UpdatedComponent->GetComponentLocation()), WorldToMetersScale);
		MovementObject.object_rotation = MagicLeap::ToMLQuat(WorldToTracking.TransformRotation(UpdatedComponent->GetComponentQuat()));
	}
}

void UMagicLeapMovementComponent::GetMovement3DofControls(MLMovement3DofControls& Movement3DofControls) const
{
	MLMovement3DofControlsInit(&Movement3DofControls);
	if (MovementController != nullptr)
	{
		const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
		const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();

		FRotator DeviceRotation;
		FVector DevicePosition;
		UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(DeviceRotation, DevicePosition);

		Movement3DofControls.headpose_position = MagicLeap::ToMLVector(DevicePosition, WorldToMetersScale);
		Movement3DofControls.control_rotation = MagicLeap::ToMLQuat(WorldToTracking.TransformRotation(MovementController->GetComponentQuat()));
	}
}

void UMagicLeapMovementComponent::GetMovement6DofControls(MLMovement6DofControls& Movement6DofControls) const
{
	MLMovement6DofControlsInit(&Movement6DofControls);
	if (MovementController != nullptr)
	{
		const float WorldToMetersScale = IMagicLeapPlugin::Get().GetWorldToMetersScale();
		const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();

		FRotator DeviceRotation;
		FVector DevicePosition;
		UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(DeviceRotation, DevicePosition);

		Movement6DofControls.headpose_position = MagicLeap::ToMLVector(DevicePosition, WorldToMetersScale);
		Movement6DofControls.headpose_rotation = MagicLeap::ToMLQuat(DeviceRotation.Quaternion());

		Movement6DofControls.control_position = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(MovementController->GetComponentLocation()), WorldToMetersScale);
		Movement6DofControls.control_rotation = MagicLeap::ToMLQuat(WorldToTracking.TransformRotation(MovementController->GetComponentQuat()));
	}
}

void UMagicLeapMovementComponent::EndCollision(MLHandle& CollisionHandle)
{
	if (MLHandleIsValid(CollisionHandle))
	{
		MLResult Result = MLMovementEndCollision(SessionHandle, CollisionHandle);
		UE_CLOG(Result != MLResult_Ok, LogMagicLeapMovement, Error, TEXT("MLMovementEndCollision failed with error '%s'"), UTF8_TO_TCHAR(MLMovementGetResultString(Result)));
		CollisionHandle = ML_INVALID_HANDLE;
	}
}

void UMagicLeapMovementComponent::EndAllSoftCollisions()
{
	for (auto& SoftCollisionPair : SoftCollisions)
	{
		EndCollision(SoftCollisionPair.Value);
	}
	SoftCollisions.Empty();

	for (MLHandle Handle : SoftCollisionsPendingDestroy)
	{
		EndCollision(Handle);
	}
	SoftCollisionsPendingDestroy.Empty();
}

#endif // WITH_MLSDK
