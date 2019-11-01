// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RaycastComponent.h"
#include "MagicLeapHMD.h"
#include "MagicLeapMath.h"
#include "AppFramework.h"
#include "Engine/Engine.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#if WITH_EDITOR
#include "Editor.h"
#endif
#include "Lumin/CAPIShims/LuminAPIRaycast.h"

class FMagicLeapRaycastTrackerImpl
{
public:
	FMagicLeapRaycastTrackerImpl()
#if WITH_MLSDK
		: Tracker(ML_INVALID_HANDLE)
#endif //WITH_MLSDK
	{};

public:
#if WITH_MLSDK
	MLHandle Tracker;
#endif //WITH_MLSDK

public:
	bool Create()
	{
#if WITH_MLSDK
		if (!MLHandleIsValid(Tracker))
		{
			MLResult Result = MLRaycastCreate(&Tracker);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogMagicLeap, Error, TEXT("MLRaycastCreate failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
				return false;
			}
		}
#endif //WITH_MLSDK
		return true;
	}

	void Destroy()
	{
#if WITH_MLSDK
		if (MLHandleIsValid(Tracker))
		{
			MLResult Result = MLRaycastDestroy(Tracker);
			UE_CLOG(Result != MLResult_Ok, LogMagicLeap, Error, TEXT("MLRaycastDestroy failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
			Tracker = ML_INVALID_HANDLE;
		}
#endif //WITH_MLSDK
	}
};

#if WITH_MLSDK
EMagicLeapRaycastResultState MLToUnrealRaycastResultState(MLRaycastResultState state)
{
	switch (state)
	{
	case MLRaycastResultState_RequestFailed:
		return EMagicLeapRaycastResultState::RequestFailed;
	case MLRaycastResultState_HitObserved:
		return EMagicLeapRaycastResultState::HitObserved;
	case MLRaycastResultState_HitUnobserved:
		return EMagicLeapRaycastResultState::HitUnobserved;
	case MLRaycastResultState_NoCollision:
		return EMagicLeapRaycastResultState::NoCollision;
	}
	return EMagicLeapRaycastResultState::RequestFailed;
}
#endif //WITH_MLSDK

UMagicLeapRaycastComponent::UMagicLeapRaycastComponent()
	: Impl(new FMagicLeapRaycastTrackerImpl())
{
	// Make sure this component ticks
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	bAutoActivate = true;

#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::PrePIEEnded.AddUObject(this, &UMagicLeapRaycastComponent::PrePIEEnded);
	}
#endif
}

UMagicLeapRaycastComponent::~UMagicLeapRaycastComponent()
{
	delete Impl;
}

void UMagicLeapRaycastComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid() && MLHandleIsValid(Impl->Tracker)))
	{
		return;
	}

	for (auto& pair : PendingRequests)
	{
		MLRaycastResult result;
		MLResult APICallResult = MLRaycastGetResult(Impl->Tracker, pair.Key, &result);
		if (APICallResult == MLResult_Ok)
		{
			const FAppFramework& AppFramework = static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
			const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

			// TODO: Should we apply this transform here or expect the user to use the result as a child of the XRPawn like the other features?
			// This being for raycast, we should probably apply the transform since the result might be used for other than just placing objects.
			const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(this);

			FMagicLeapRaycastHitResult hitResult;
			hitResult.HitState = MLToUnrealRaycastResultState(result.state);
			hitResult.HitPoint = TrackingToWorld.TransformPosition(MagicLeap::ToFVector(result.hitpoint, WorldToMetersScale));
			hitResult.Normal = TrackingToWorld.TransformVectorNoScale(MagicLeap::ToFVector(result.normal, 1.0f));
			hitResult.Confidence = result.confidence;
			hitResult.UserData = pair.Value.UserData;

			if (hitResult.HitPoint.ContainsNaN() || hitResult.Normal.ContainsNaN())
			{
				UE_LOG(LogMagicLeap, Error, TEXT("Raycast result contains NaNs."));
				hitResult.HitState = EMagicLeapRaycastResultState::RequestFailed;
			}

			pair.Value.ResultDelegate.ExecuteIfBound(hitResult);
			CompletedRequests.Add(pair.Key);
		}
		else if (APICallResult != MLResult_Pending)
		{
			UE_LOG(LogMagicLeap, Error, TEXT("MLRaycastGetResult failed with result %s."), UTF8_TO_TCHAR(MLGetResultString(APICallResult)));
		}
	}

	// TODO: Implement better strategy to optimize memory allocation.
	if (CompletedRequests.Num() > 0)
	{
		for (MLHandle handle : CompletedRequests)
		{
			PendingRequests.Remove(handle);
		}
		CompletedRequests.Empty();
	}
#endif //WITH_MLSDK
}

bool UMagicLeapRaycastComponent::RequestRaycast(const FMagicLeapRaycastQueryParams& RequestParams, const FRaycastResultDelegate& ResultDelegate)
{
#if WITH_MLSDK
	if (!(IMagicLeapPlugin::Get().IsMagicLeapHMDValid() && Impl->Create()))
	{
		return false;
	}

	const FAppFramework& AppFramework = static_cast<FMagicLeapHMD*>(GEngine->XRSystem->GetHMDDevice())->GetAppFrameworkConst();
	const float WorldToMetersScale = AppFramework.GetWorldToMetersScale();

	const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(this).Inverse();

	MLRaycastQuery query;
	query.position = MagicLeap::ToMLVector(WorldToTracking.TransformPosition(RequestParams.Position), WorldToMetersScale);
	query.direction = MagicLeap::ToMLVectorNoScale(WorldToTracking.TransformVectorNoScale(RequestParams.Direction));
	query.up_vector = MagicLeap::ToMLVectorNoScale(WorldToTracking.TransformVectorNoScale(RequestParams.UpVector));
	query.width = static_cast<uint32>(RequestParams.Width);
	query.height = static_cast<uint32>(RequestParams.Height);
	query.collide_with_unobserved = RequestParams.CollideWithUnobserved;
	query.horizontal_fov_degrees = RequestParams.HorizontalFovDegrees;

	MLHandle Handle = ML_INVALID_HANDLE;
	MLResult Result = MLRaycastRequest(Impl->Tracker, &query, &Handle);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogMagicLeap, Error, TEXT("MLRaycastRequest failed with error %s."), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return false;
	}

	FRaycastRequestMetaData& requestMetaData = PendingRequests.Add(Handle);
	requestMetaData.UserData = RequestParams.UserData;
	requestMetaData.ResultDelegate = ResultDelegate;
#endif //WITH_MLSDK

	return true;
}

void UMagicLeapRaycastComponent::FinishDestroy()
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		FEditorDelegates::PrePIEEnded.RemoveAll(this);
	}
#endif
	Impl->Destroy();
	Super::FinishDestroy();
}

#if WITH_EDITOR
void UMagicLeapRaycastComponent::PrePIEEnded(bool bWasSimulatingInEditor)
{
	Impl->Destroy();
}
#endif

FMagicLeapRaycastQueryParams UMagicLeapRaycastFunctionLibrary::MakeRaycastQueryParams(FVector Position, FVector Direction, FVector UpVector, int32 Width, int32 Height, float HorizontalFovDegrees, bool CollideWithUnobserved, int32 UserData)
{
	FMagicLeapRaycastQueryParams QueryParams;
	QueryParams.Position = Position;
	QueryParams.Direction = Direction;
	QueryParams.UpVector = UpVector;
	QueryParams.Width = Width;
	QueryParams.Height = Height;
	QueryParams.HorizontalFovDegrees = HorizontalFovDegrees;
	QueryParams.CollideWithUnobserved = CollideWithUnobserved;
	QueryParams.UserData = UserData;
	return QueryParams;
}
