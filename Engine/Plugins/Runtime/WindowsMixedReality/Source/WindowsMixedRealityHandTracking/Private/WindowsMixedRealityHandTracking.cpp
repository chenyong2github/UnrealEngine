// Copyright Epic Games, Inc. All Rights Reserved.

#include "WindowsMixedRealityHandTracking.h"
#include "IWindowsMixedRealityHMDPlugin.h"
#include "Framework/Application/SlateApplication.h"
#include "CoreMinimal.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Engine.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "IWindowsMixedRealityHandTrackingPlugin.h"
#include "ILiveLinkClient.h"

#define LOCTEXT_NAMESPACE "WindowsMixedRealityHandTracking"

class FWindowsMixedRealityHandTrackingModule :
	public IWindowsMixedRealityHandTrackingModule
{
public:
	FWindowsMixedRealityHandTrackingModule()
		: InputDevice(nullptr)
		, bLiveLinkSourceRegistered(false)
	{}

	virtual void StartupModule() override
	{
		IWindowsMixedRealityHandTrackingModule::StartupModule();

		// HACK: Generic Application might not be instantiated at this point so we create the input device with a
		// dummy message handler. When the Generic Application creates the input device it passes a valid message
		// handler to it which is further on used for all the controller events. This hack fixes issues caused by
		// using a custom input device before the Generic Application has instantiated it. Eg. within BeginPlay()
		//
		// This also fixes the warnings that pop up on the custom input keys when the blueprint loads. Those
		// warnings are caused because Unreal loads the bluerints before the input device has been instantiated
		// and has added its keys, thus leading Unreal to believe that those keys don't exist. This hack causes
		// an earlier instantiation of the input device, and consequently, the custom keys.
		TSharedPtr<FGenericApplicationMessageHandler> DummyMessageHandler(new FGenericApplicationMessageHandler());
		CreateInputDevice(DummyMessageHandler.ToSharedRef());
	}

	virtual void ShutdownModule() override
	{
		IWindowsMixedRealityHandTrackingModule::ShutdownModule();
	}

	virtual TSharedPtr<class IInputDevice> CreateInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler) override
	{
		if (!InputDevice.IsValid())
		{
			TSharedPtr<FWindowsMixedRealityHandTracking> HandTrackingInputDevice(new FWindowsMixedRealityHandTracking(InMessageHandler));
			InputDevice = HandTrackingInputDevice;

			return InputDevice;
		}
		else
		{
			InputDevice.Get()->SetMessageHandler(InMessageHandler);
			return InputDevice;
		}
		return nullptr;
	}

	virtual TSharedPtr<IInputDevice> GetInputDevice() override
	{
		if (!InputDevice.IsValid())
		{
			CreateInputDevice(FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler());
		}
		return InputDevice;
	}

	virtual TSharedPtr<ILiveLinkSource> GetLiveLinkSource() override
	{
		if (!InputDevice.IsValid())
		{
			CreateInputDevice(FSlateApplication::Get().GetPlatformApplication()->GetMessageHandler());
		}
		return InputDevice;
	}

	virtual bool IsLiveLinkSourceValid() const override
	{
		return InputDevice.IsValid();
	}

	virtual void AddLiveLinkSource() override
	{
		if (bLiveLinkSourceRegistered)
		{
			return;
		}
		// Auto register with LiveLink
		ensureMsgf(FModuleManager::Get().LoadModule("LiveLink"), TEXT("WindowsMixedRealityHandTracking depends on the LiveLink module."));
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			ILiveLinkClient* LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
			LiveLinkClient->AddSource(GetLiveLinkSource());
			bLiveLinkSourceRegistered = true;
		}
	}

	virtual void RemoveLiveLinkSource() override
	{
		IModularFeatures& ModularFeatures = IModularFeatures::Get();
		if (ModularFeatures.IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
		{
			ILiveLinkClient* LiveLinkClient = &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
			LiveLinkClient->RemoveSource(GetLiveLinkSource());
		}
		bLiveLinkSourceRegistered = false;
	}

private:
	TSharedPtr<FWindowsMixedRealityHandTracking> InputDevice;
	bool bLiveLinkSourceRegistered;
};

IMPLEMENT_MODULE(FWindowsMixedRealityHandTrackingModule, WindowsMixedRealityHandTracking);


FLiveLinkSubjectName FWindowsMixedRealityHandTracking::LiveLinkLeftHandTrackingSubjectName(TEXT("WMRLeftHand"));
FLiveLinkSubjectName FWindowsMixedRealityHandTracking::LiveLinkRightHandTrackingSubjectName(TEXT("WMRRightHand"));


FWindowsMixedRealityHandTracking::FWindowsMixedRealityHandTracking(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
	: MessageHandler(InMessageHandler)
	, DeviceIndex(0)
{
	// Register "MotionController" modular feature manually
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	AddKeys();

	// We're implicitly requiring that the WindowsMixedRealityPlugin has been loaded and
	// initialized at this point.
	if (!IWindowsMixedRealityHMDPlugin::Get().IsAvailable())
	{
		UE_LOG(LogWindowsMixedRealityHandTracking, Error, TEXT("Error - WMRHMDPlugin isn't available"));
	}
}

FWindowsMixedRealityHandTracking::~FWindowsMixedRealityHandTracking()
{
	// Normally, the WindowsMixedRealityPlugin will be around during unload,
	// but it isn't an assumption that we should make.
	if (IWindowsMixedRealityHMDPlugin::IsAvailable())
	{
// 		auto HMD = IWindowsMixedRealityHMDPlugin::Get().GetHMD().Pin();
// 		if (HMD.IsValid())
// 		{
// 			HMD->UnregisterWindowsMixedRealityInputDevice(this);
// 		}
	}

//	Disable();

	// Unregister "MotionController" modular feature manually
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

FWindowsMixedRealityHandTracking::FHandState::FHandState()
{
}

bool FWindowsMixedRealityHandTracking::FHandState::GetTransform(EWMRHandKeypoint Keypoint, FTransform& OutTransform) const
{
	check((int32)Keypoint < EWMRHandKeypointCount);
	OutTransform = KeypointTransforms[(uint32)Keypoint];
	
	return ReceivedJointPoses;
}

const FTransform& FWindowsMixedRealityHandTracking::FHandState::GetTransform(EWMRHandKeypoint Keypoint) const
{
	check((int32)Keypoint < EWMRHandKeypointCount);
	return KeypointTransforms[(uint32)Keypoint];
}

bool FWindowsMixedRealityHandTracking::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	bool bTracked = false;
	if (ControllerIndex == DeviceIndex)
	{
		FTransform ControllerTransform = FTransform::Identity;
		if (MotionSource == FName("Left"))
		{
			ControllerTransform = GetLeftHandState().GetTransform(EWMRHandKeypoint::Palm);
			bTracked = GetLeftHandState().ReceivedJointPoses;
		}
		else if (MotionSource == FName("Right"))
		{
			ControllerTransform = GetRightHandState().GetTransform(EWMRHandKeypoint::Palm);
			bTracked = GetRightHandState().ReceivedJointPoses;
		}

		// This can only be done in the game thread since it uses the UEnum directly
		if (IsInGameThread())
		{
			const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EWMRHandKeypoint"), true);
			check(EnumPtr != nullptr);
			bool bUseRightHand = false;
			FString SourceString = MotionSource.ToString();
			if (SourceString.StartsWith((TEXT("Right"))))
			{
				bUseRightHand = true;
				// Strip off the Right
				SourceString.RightInline(SourceString.Len() - 5, false);
			}
			else
			{
				// Strip off the Left
				SourceString.RightInline(SourceString.Len() - 4, false);
			}
			FName FullEnumName(*FString(TEXT("EWMRHandKeypoint::") + SourceString), FNAME_Find);
			// Get the enum value from the name
			int32 ValueFromName = EnumPtr->GetValueByName(FullEnumName);
			if (ValueFromName != INDEX_NONE)
			{
				if (bUseRightHand)
				{
					ControllerTransform = GetRightHandState().GetTransform((EWMRHandKeypoint)ValueFromName);
					bTracked = GetRightHandState().ReceivedJointPoses;
				}
				else
				{
					ControllerTransform = GetLeftHandState().GetTransform((EWMRHandKeypoint)ValueFromName);
					bTracked = GetLeftHandState().ReceivedJointPoses;
				}
			}
		}

		OutPosition = ControllerTransform.GetLocation();
		OutOrientation = ControllerTransform.GetRotation().Rotator();
	}

	// Then call super to handle a few of the default labels, for backward compatibility
	FXRMotionControllerBase::GetControllerOrientationAndPosition(ControllerIndex, MotionSource, OutOrientation, OutPosition, WorldToMetersScale);

	return bTracked;
}

bool FWindowsMixedRealityHandTracking::GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	bool bControllerTracked = false;
	if (ControllerIndex == DeviceIndex)
	{
		if (GetControllerTrackingStatus(ControllerIndex, DeviceHand) != ETrackingStatus::NotTracked)
		{
			const FTransform* ControllerTransform = nullptr;

			if (DeviceHand == EControllerHand::Left)
			{
				ControllerTransform = &GetLeftHandState().GetTransform(EWMRHandKeypoint::Palm);
			}
			else if (DeviceHand == EControllerHand::Right)
			{
				ControllerTransform = &GetRightHandState().GetTransform(EWMRHandKeypoint::Palm);
			}

			if (ControllerTransform != nullptr)
			{
				OutPosition = ControllerTransform->GetLocation();
				OutOrientation = ControllerTransform->GetRotation().Rotator();

				bControllerTracked = true;
			}
		}
	}

	return bControllerTracked;
}

ETrackingStatus FWindowsMixedRealityHandTracking::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
{
	const FWindowsMixedRealityHandTracking::FHandState& HandState = (DeviceHand == EControllerHand::Left) ? GetLeftHandState() : GetRightHandState();

	return HandState.ReceivedJointPoses ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
}

FName FWindowsMixedRealityHandTracking::GetMotionControllerDeviceTypeName() const
{
	const static FName DefaultName(TEXT("WindowsMixedRealityHandTracking"));
	return DefaultName;
}

void FWindowsMixedRealityHandTracking::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
	check(IsInGameThread());

	SourcesOut.Empty(EWMRHandKeypointCount);

	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, TEXT("EWMRHandKeypoint"), true);
	check(EnumPtr != nullptr);
	for (int32 Keypoint = 0; Keypoint < EWMRHandKeypointCount; Keypoint++)
	{
		SourcesOut.Add(FWindowsMixedRealityHandTracking::ParseEWMRHandKeypointEnumName(EnumPtr->GetNameByValue(Keypoint)));
	}
}

void FWindowsMixedRealityHandTracking::Tick(float DeltaTime)
{
	UpdateTrackerData();
}

void FWindowsMixedRealityHandTracking::SendControllerEvents()
{
// @TODO: implement for WMRSDK
// #if WITH_MLSDK
// 	{
// 		const MLHandTrackingData& CurrentHandTrackingData = GetCurrentHandTrackingData();
// 		const MLHandTrackingData& OldHandTrackingData = GetPreviousHandTrackingData();
// 
// 		SendControllerEventsForHand(CurrentHandTrackingData.left_hand_state, OldHandTrackingData.left_hand_state, LeftStaticGestureMap);
// 		SendControllerEventsForHand(CurrentHandTrackingData.right_hand_state, OldHandTrackingData.right_hand_state, RightStaticGestureMap);
// 	}
// #endif //WITH_MLSDK
}

void FWindowsMixedRealityHandTracking::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

bool FWindowsMixedRealityHandTracking::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

bool FWindowsMixedRealityHandTracking::IsGamepadAttached() const
{
	return false;
}

const FWindowsMixedRealityHandTracking::FHandState& FWindowsMixedRealityHandTracking::GetLeftHandState() const
{
	return HandStates[0];
}

const FWindowsMixedRealityHandTracking::FHandState& FWindowsMixedRealityHandTracking::GetRightHandState() const
{
	return HandStates[1];
}

bool FWindowsMixedRealityHandTracking::IsHandTrackingStateValid() const
{
	return true;
}

bool FWindowsMixedRealityHandTracking::GetKeypointTransform(EControllerHand Hand, EWMRHandKeypoint Keypoint, FTransform& OutTransform) const
{
	const FWindowsMixedRealityHandTracking::FHandState& HandState = (Hand == EControllerHand::Left) ? GetLeftHandState() : GetRightHandState();

	return HandState.GetTransform(Keypoint, OutTransform);
}

void FWindowsMixedRealityHandTracking::UpdateTrackerData()
{
#if WITH_WINDOWS_MIXED_REALITY
	// Pump the interop update of the hand.
	WindowsMixedReality::FWindowsMixedRealityStatics::PollHandTracking();

	if (IWindowsMixedRealityHMDPlugin::Get().IsAvailable())
	{
		// Get all the bones for each hand
		for (int32 Hand = 0; Hand < 2; Hand++)
		{
			// Might need this....
// 			FRotator rot;
// 			FVector pos;
// 			WindowsMixedReality::FWindowsMixedRealityStatics::GetControllerOrientationAndPosition(static_cast<WindowsMixedReality::MixedRealityInterop::HMDHand>(Hand), rot, pos);
// 			FTransform ControllerTransform(rot, pos);

			for (int32 Keypoint = 0; Keypoint < EWMRHandKeypointCount; Keypoint++)
			{
				FRotator Orientation;
				FVector Position;
				if (WindowsMixedReality::FWindowsMixedRealityStatics::GetHandJointOrientationAndPosition(
					static_cast<WindowsMixedReality::HMDHand>(Hand),
					static_cast<WindowsMixedReality::HMDHandJoint>(Keypoint),
					Orientation,
					Position))
				{
					HandStates[Hand].KeypointTransforms[Keypoint] = FTransform(Orientation, Position);
					HandStates[Hand].ReceivedJointPoses = true;
				}
				else
				{
					HandStates[Hand].ReceivedJointPoses = false;
				}
			}
		}

		UpdateLiveLink();
	}
#endif
}

void FWindowsMixedRealityHandTracking::AddKeys()
{
}

void FWindowsMixedRealityHandTracking::ConditionallyEnable()
{
	// @TODO: fix
}

#undef LOCTEXT_NAMESPACE
