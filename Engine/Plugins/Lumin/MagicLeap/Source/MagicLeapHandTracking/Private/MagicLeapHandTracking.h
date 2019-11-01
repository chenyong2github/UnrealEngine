// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/IInputInterface.h"
#include "IInputDevice.h"
#include "IMagicLeapTrackerEntity.h"
#include "Lumin/CAPIShims/LuminAPIHandTracking.h"

#include "MagicLeapHandTrackingTypes.h"
#include "XRMotionControllerBase.h"
#include "InputCoreTypes.h"
#include "AppEventHandler.h"
#include "ILiveLinkSource.h"

/**
  * MagicLeap HandTracking
  */
class FMagicLeapHandTracking : public IInputDevice, public IMagicLeapTrackerEntity, public FXRMotionControllerBase, public MagicLeap::IAppEventHandler, public ILiveLinkSource
{
public:
	static FName HandCenter_Name;

	static FName WristCenter_Name;
	static FName WristUlnar_Name;
	static FName WristRadial_Name;

	static FName ThumbTip_Name;
	static FName ThumbIP_Name;
	static FName ThumbMCP_Name;
	static FName ThumbCMC_Name;

	static FName IndexFingerTip_Name;
	static FName IndexFingerDIP_Name;
	static FName IndexFingerPIP_Name;
	static FName IndexFingerMCP_Name;

	static FName MiddleFingerTip_Name;
	static FName MiddleFingerDIP_Name;
	static FName MiddleFingerPIP_Name;
	static FName MiddleFingerMCP_Name;

	static FName RingFingerTip_Name;
	static FName RingFingerDIP_Name;
	static FName RingFingerPIP_Name;
	static FName RingFingerMCP_Name;

	static FName PinkyFingerTip_Name;
	static FName PinkyFingerDIP_Name;
	static FName PinkyFingerPIP_Name;
	static FName PinkyFingerMCP_Name;


	static FName LeftHandCenter_Name;

	static FName LeftWristCenter_Name;
	static FName LeftWristUlnar_Name;
	static FName LeftWristRadial_Name;

	static FName LeftThumbTip_Name;
	static FName LeftThumbIP_Name;
	static FName LeftThumbMCP_Name;
	static FName LeftThumbCMC_Name;

	static FName LeftIndexFingerTip_Name;
	static FName LeftIndexFingerDIP_Name;
	static FName LeftIndexFingerPIP_Name;
	static FName LeftIndexFingerMCP_Name;

	static FName LeftMiddleFingerTip_Name;
	static FName LeftMiddleFingerDIP_Name;
	static FName LeftMiddleFingerPIP_Name;
	static FName LeftMiddleFingerMCP_Name;

	static FName LeftRingFingerTip_Name;
	static FName LeftRingFingerDIP_Name;
	static FName LeftRingFingerPIP_Name;
	static FName LeftRingFingerMCP_Name;

	static FName LeftPinkyFingerTip_Name;
	static FName LeftPinkyFingerDIP_Name;
	static FName LeftPinkyFingerPIP_Name;
	static FName LeftPinkyFingerMCP_Name;


	static FName RightHandCenter_Name;

	static FName RightWristCenter_Name;
	static FName RightWristUlnar_Name;
	static FName RightWristRadial_Name;

	static FName RightThumbTip_Name;
	static FName RightThumbIP_Name;
	static FName RightThumbMCP_Name;
	static FName RightThumbCMC_Name;

	static FName RightIndexFingerTip_Name;
	static FName RightIndexFingerDIP_Name;
	static FName RightIndexFingerPIP_Name;
	static FName RightIndexFingerMCP_Name;

	static FName RightMiddleFingerTip_Name;
	static FName RightMiddleFingerDIP_Name;
	static FName RightMiddleFingerPIP_Name;
	static FName RightMiddleFingerMCP_Name;

	static FName RightRingFingerTip_Name;
	static FName RightRingFingerDIP_Name;
	static FName RightRingFingerPIP_Name;
	static FName RightRingFingerMCP_Name;

	static FName RightPinkyFingerTip_Name;
	static FName RightPinkyFingerDIP_Name;
	static FName RightPinkyFingerPIP_Name;
	static FName RightPinkyFingerMCP_Name;

	struct FTransformRecord
	{
		FTransform Transform = FTransform::Identity;
		bool bWritten = false;
	};
	struct FWristTransforms : public FNoncopyable
	{
		// Wrist center.
		FTransformRecord Center;
		// Ulnar-sided wrist
		FTransformRecord Ulnar;
		// Radial-sided wrist.
		FTransformRecord Radial;
	};
	struct FDigitTransforms : public FNoncopyable
	{
		// These labels are not correct anatomical nomenclature for the thumb, but they 1:1 map with the correct names.
		FTransformRecord Tip; // digit tip
		FTransformRecord DIP; // distal joint
		FTransformRecord PIP; // proximal joint
		FTransformRecord MCP; // base of digit
	};
	struct FHandState : public FNoncopyable
	{
		FHandState();

		EMagicLeapHandTrackingGesture Gesture = EMagicLeapHandTrackingGesture::NoHand;
		float GestureConfidence = 0.0f;
		FVector HandCenterNormalized = FVector::ZeroVector;

		FDigitTransforms Thumb;
		FDigitTransforms IndexFinger;
		FDigitTransforms MiddleFinger;
		FDigitTransforms RingFinger;
		FDigitTransforms PinkyFinger;

		FWristTransforms Wrist;

		FTransformRecord HandCenter;

		bool IsValid() const { return Gesture != EMagicLeapHandTrackingGesture::NoHand; }
		bool GetTransform(EMagicLeapHandTrackingKeypoint KeyPoint, FTransform& OutTransform) const;
		const FTransformRecord& GetTransform(EMagicLeapHandTrackingKeypoint KeyPoint) const;

	private:
		FTransformRecord* EnumToTransformMap[EMagicLeapHandTrackingKeypointCount];
	};

public:
	FMagicLeapHandTracking(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler);
	virtual ~FMagicLeapHandTracking();

	/** IMotionController interface */
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const override;
	virtual bool GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const override;
	virtual ETrackingStatus GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const override;
	virtual FName GetMotionControllerDeviceTypeName() const override;
	virtual void EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const override;

	// ILiveLinkSource interface
	virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
	virtual bool IsSourceStillValid() const override;
	virtual bool RequestSourceShutdown() override;
	virtual FText GetSourceMachineName() const override;
	virtual FText GetSourceStatus() const override;
	virtual FText GetSourceType() const override;
	// End ILiveLinkSource

	/** IInputDevice interface */
	virtual void Tick(float DeltaTime) override;
	virtual void SendControllerEvents() override;
	virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override;
	virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
	virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {};
	virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override {};
	virtual bool IsGamepadAttached() const override;

	/** IMagicLeapTrackerEntity interface */
	virtual void DestroyEntityTracker() override;
	virtual void OnBeginRendering_GameThread() override;

	const FHandState& GetLeftHandState() const;
	const FHandState& GetRightHandState() const;
	bool IsHandTrackingStateValid() const;

	bool GetKeypointTransform(EControllerHand Hand, EMagicLeapHandTrackingKeypoint Keypoint, FTransform& OutTransform) const;

	bool SetConfiguration(bool bTrackingEnabled, const TArray<EMagicLeapHandTrackingGesture>& ActiveKeyPoses, EMagicLeapHandTrackingKeypointFilterLevel KeypointsFilterLevel, EMagicLeapHandTrackingGestureFilterLevel GestureFilterLevel);
	bool GetConfiguration(bool& bTrackingEnabled, TArray<EMagicLeapHandTrackingGesture>& ActiveKeyPoses, EMagicLeapHandTrackingKeypointFilterLevel& KeypointsFilterLevel, EMagicLeapHandTrackingGestureFilterLevel& GestureFilterLevel);

	void SetGestureConfidenceThreshold(EMagicLeapHandTrackingGesture Gesture, float Confidence);
	float GetGestureConfidenceThreshold(EMagicLeapHandTrackingGesture Gesture) const;

private:
	void BuildKeypointMaps();
	const FTransform* FindTransformBySource(FName SourceName) const;
	const FHandState* FindHandBySource(FName SourceName) const;
	void UpdateTrackerData();
	void UpdateCurrentHandTrackerTransforms();
	void AddKeys();
	void ConditionallyEnable();
	void OnAppPause() override;
	void OnAppResume() override;

	void SetupLiveLinkData(struct FLiveLinkSkeletonStaticData& StaticData);
	void UpdateLiveLink();
	void UpdateLiveLinkTransforms(TArray<FTransform>& OutTransforms, const FMagicLeapHandTracking::FHandState& HandState);

#if WITH_MLSDK
	const MLHandTrackingData& GetCurrentHandTrackingData()				{ return HandTrackingDatas[CurrentHandTrackingDataIndex]; }
	const MLHandTrackingData& GetPreviousHandTrackingData()				{ return HandTrackingDatas[1 - CurrentHandTrackingDataIndex]; }
	void SendControllerEventsForHand(const MLHandTrackingHandState& NewHandState, const MLHandTrackingHandState& OldHandState, const TArray<FName>& GestureMap);

	static EMagicLeapHandTrackingGesture TranslateGestureEnum(MLHandTrackingKeyPose KeyPose);
#endif //WITH_MLSDK

	TSharedPtr<FGenericApplicationMessageHandler> MessageHandler;
	int32 DeviceIndex;

	bool HandTrackingPipelineEnabled;

#if WITH_MLSDK
	MLHandle HandTracker;
	MLHandTrackingData HandTrackingDatas[2];
	int32 CurrentHandTrackingDataIndex = 0;
	MLHandTrackingStaticData HandTrackingStaticData;
#endif //WITH_MLSDK
	TArray<int32> BoneParents;
	TArray<EMagicLeapHandTrackingKeypoint> BoneKeypoints;

	FHandState LeftHand;
	FHandState RightHand;

	bool bIsHandTrackingStateValid;

	TArray<float> GestureConfidenceThresholds;

	TArray<FName> LeftStaticGestureMap;
	TArray<FName> RightStaticGestureMap;

	TMap<FName, FTransform*> SourceToTransformMap;
	TMap<FName, FHandState*> SourceToHandMap;

	struct FStaticHandTracking
	{
		static const FKey Left_Finger;
		static const FKey Left_Fist;
		static const FKey Left_Pinch;
		static const FKey Left_Thumb;
		static const FKey Left_L;
		static const FKey Left_OpenHand;
		// DEPRECATED
		static const FKey Left_OpenHandBack;
		static const FKey Left_Ok;
		static const FKey Left_C;
		static const FKey Left_NoPose;
		static const FKey Left_NoHand;

		static const FKey Right_Finger;
		static const FKey Right_Fist;
		static const FKey Right_Pinch;
		static const FKey Right_Thumb;
		static const FKey Right_L;
		static const FKey Right_OpenHand;
		// DEPRECATED
		static const FKey Right_OpenHandBack;
		static const FKey Right_Ok;
		static const FKey Right_C;
		static const FKey Right_NoPose;
		static const FKey Right_NoHand;
	};

	// LiveLink Data
	/** The local client to push data updates to */
	ILiveLinkClient* LiveLinkClient = nullptr;
	/** Our identifier in LiveLink */
	FGuid LiveLinkSourceGuid;

	static FName LiveLinkLeftHandTrackingSubjectName;
	static FName LiveLinkRightHandTrackingSubjectName;
	bool bNewLiveLinkClient = false;

	FLiveLinkFrameDataStruct LiveLinkLeftFrame;
	FLiveLinkFrameDataStruct LiveLinkRightFrame;
};

DEFINE_LOG_CATEGORY_STATIC(LogMagicLeapHandTracking, Display, All);
