// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/IInputInterface.h"

// @TODO: check for Windows 10 SDK version >= 10.0.18309.0 for hand tracking

#include "WindowsMixedRealityStatics.h"
#include "WindowsMixedRealityHandTrackingTypes.h"
#include "XRMotionControllerBase.h"
#include "InputCoreTypes.h"
#include "ILiveLinkSource.h"
#include "IInputDevice.h"
#include "Roles/LiveLinkAnimationTypes.h"

/**
  * WindowsMixedReality HandTracking
  */
class FWindowsMixedRealityHandTracking :
	public IInputDevice,
	public FXRMotionControllerBase,
	public ILiveLinkSource
{
public:
	struct FHandState : public FNoncopyable
	{
		FHandState();

		FTransform KeypointTransforms[EWMRHandKeypointCount];

		bool GetTransform(EWMRHandKeypoint KeyPoint, FTransform& OutTransform) const;
		const FTransform& GetTransform(EWMRHandKeypoint KeyPoint) const;
	};

public:
	FWindowsMixedRealityHandTracking(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FWindowsMixedRealityHandTracking();

	/** IMotionController interface */
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override;
	virtual FName GetMotionControllerDeviceTypeName() const override;
	virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;

	// ILiveLinkSource interface
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;
	virtual FText GetSourceType() const override;
	// End ILiveLinkSource

	/** IWindowsMixedRealityInputDevice interface */
	virtual void Tick(float DeltaTime) override;
	virtual void SendControllerEvents() override;
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {};
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override {};
	virtual bool IsGamepadAttached() const override;

	const FHandState& GetLeftHandState() const;
	const FHandState& GetRightHandState() const;
	bool IsHandTrackingStateValid() const;

	bool GetKeypointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const;

	/** Parses the enum name removing the prefix */
	static FName ParseEWMRHandKeypointEnumName(FName EnumName)
	{
		static int32 EnumNameLength = FString(TEXT("EWMRHandKeypoint::")).Len();
		FString EnumString = EnumName.ToString();
		return FName(*EnumString.Right(EnumString.Len() - EnumNameLength));
	}

private:
	void UpdateTrackerData();
	void AddKeys();
	void ConditionallyEnable();
	
	void SetupLiveLinkData();
	void UpdateLiveLink();
	void UpdateLiveLinkTransforms(TArray<FTransform>& OutTransforms, const FWindowsMixedRealityHandTracking::FHandState& HandState);

	TSharedPtr<FGenericApplicationMessageHandler> MessageHandler;
	int32 DeviceIndex;

	int32 CurrentHandTrackingDataIndex = 0;

	TArray<int32> BoneParents;
	TArray<EWMRHandKeypoint> BoneKeypoints;

	FHandState HandStates[2];

	bool bIsHandTrackingStateValid;

	// LiveLink Data
	/** The local client to push data updates to */
	ILiveLinkClient* LiveLinkClient = nullptr;
	/** Our identifier in LiveLink */
	FGuid LiveLinkSourceGuid;

	static FLiveLinkSubjectName LiveLinkLeftHandTrackingSubjectName;
	static FLiveLinkSubjectName LiveLinkRightHandTrackingSubjectName;
	FLiveLinkSubjectKey LiveLinkLeftHandTrackingSubjectKey;
	FLiveLinkSubjectKey LiveLinkRightHandTrackingSubjectKey;
	bool bNewLiveLinkClient = false;
	FLiveLinkStaticDataStruct LiveLinkSkeletonStaticData;

	TArray<FTransform> LeftAnimationTransforms;
	TArray<FTransform> RightAnimationTransforms;
};

DEFINE_LOG_CATEGORY_STATIC(LogWindowsMixedRealityHandTracking, Display, All);
