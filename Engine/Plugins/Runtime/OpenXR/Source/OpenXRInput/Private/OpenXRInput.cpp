// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OpenXRInput.h"
#include "OpenXRHMD.h"
#include "OpenXRHMDPrivate.h"
#include "XRInputSettings.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/InputSettings.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Editor.h"
#endif

#include <openxr/openxr.h>

#define LOCTEXT_NAMESPACE "OpenXR"

FSimpleMulticastDelegate UXRInputSettings::OnSuggestedBindingsChanged;

#if WITH_EDITOR
void UXRInputSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	OnSuggestedBindingsChanged.Broadcast();
}
#endif

FORCEINLINE XrPath GetPath(XrInstance Instance, const char* PathString)
{
	XrPath Path = XR_NULL_PATH;
	XrResult Result = xrStringToPath(Instance, PathString, &Path);
	check(XR_SUCCEEDED(Result));
	return Path;
}

FORCEINLINE XrPath GetPath(XrInstance Instance, FString PathString)
{
	FTCHARToUTF8 EngineNameConverter(*PathString);
	return GetPath(Instance, EngineNameConverter.Get());
}

FORCEINLINE void FilterActionName(const char* InActionName, char* OutActionName)
{
	// Ensure the action name is a well-formed path
	size_t i;
	for (i = 0; InActionName[i] != '\0' && i < XR_MAX_ACTION_NAME_SIZE - 1; i++)
	{
		unsigned char c = InActionName[i];
		OutActionName[i] = (c == ' ') ? '-' : isalnum(c) ? tolower(c) : '_';
	}
	OutActionName[i] = '\0';
}

TSharedPtr< class IInputDevice > FOpenXRInputPlugin::CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	if (InputDevice)
		InputDevice->SetMessageHandler(InMessageHandler);
	return InputDevice;
}

IMPLEMENT_MODULE(FOpenXRInputPlugin, OpenXRInput)

FOpenXRInputPlugin::FOpenXRInputPlugin()
	: InputDevice()
{
}

FOpenXRInputPlugin::~FOpenXRInputPlugin()
{
}

FOpenXRHMD* FOpenXRInputPlugin::GetOpenXRHMD() const
{
	static FName SystemName(TEXT("OpenXR"));
	if (GEngine->XRSystem.IsValid() && (GEngine->XRSystem->GetSystemName() == SystemName))
	{
		return static_cast<FOpenXRHMD*>(GEngine->XRSystem.Get());
	}

	return nullptr;
}

void FOpenXRInputPlugin::StartupModule()
{
	IOpenXRInputPlugin::StartupModule();

	FOpenXRHMD* OpenXRHMD = GetOpenXRHMD();
	if (OpenXRHMD)
	{
		InputDevice = MakeShared<FOpenXRInput>(OpenXRHMD);
	}

#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");

	// While this should usually be true, it's not guaranteed that the settings module will be loaded in the editor.
	// UBT allows setting bBuildDeveloperTools to false while bBuildEditor can be true.
	// The former option indirectly controls loading of the "Settings" module.
	if (SettingsModule)
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "OpenXR Input",
			LOCTEXT("XRInputSettingsName", "OpenXR Input"),
			LOCTEXT("XRInputSettingsDescription", "Configure input for OpenXR."),
			GetMutableDefault<UXRInputSettings>()
		);
	}
#endif
}

void FOpenXRInputPlugin::ShutdownModule()
{
	IOpenXRInputPlugin::ShutdownModule();

#if WITH_EDITOR
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "OpenXR Input");
	}
#endif
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InActionSet, XrActionType InActionType, const FName& InName)
	: Set(InActionSet)
	, Type(InActionType)
	, Name(InName)
	, Keys()
	, Handle(XR_NULL_HANDLE)
{
	char ActionName[NAME_SIZE];
	Name.GetPlainANSIString(ActionName);

	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	FilterActionName(ActionName, Info.actionName);
	Info.actionType = Type;
	Info.countSubactionPaths = 0;
	Info.subactionPaths = nullptr;
	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, ActionName);
	XR_ENSURE(xrCreateAction(Set, &Info, &Handle));
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InSet, const TArray<FInputActionKeyMapping>& InActionKeys)
	: FOpenXRAction(InSet, XR_ACTION_TYPE_BOOLEAN_INPUT, InActionKeys[0].ActionName)
{
	for (auto Mapping : InActionKeys)
	{
		Keys.Add(Mapping.Key);
	}
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InSet, const TArray<FInputAxisKeyMapping>& InAxisKeys)
	: FOpenXRAction(InSet, XR_ACTION_TYPE_FLOAT_INPUT, InAxisKeys[0].AxisName)
{
	for (auto Mapping : InAxisKeys)
	{
		Keys.Add(Mapping.Key);
	}
}

FOpenXRInputPlugin::FOpenXRController::FOpenXRController(FOpenXRHMD* HMD, XrActionSet InActionSet, const char* InName)
	: ActionSet(InActionSet)
	, Action(XR_NULL_HANDLE)
	, VibrationAction(XR_NULL_HANDLE)
	, DeviceId(-1)
{
	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, InName);
	FCStringAnsi::Strcat(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, " Pose");
	FilterActionName(Info.localizedActionName, Info.actionName);
	Info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	Info.countSubactionPaths = 0;
	Info.subactionPaths = nullptr;
	XR_ENSURE(xrCreateAction(ActionSet, &Info, &Action));

	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, InName);
	FCStringAnsi::Strcat(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, " Vibration");
	FilterActionName(Info.localizedActionName, Info.actionName);
	Info.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	XR_ENSURE(xrCreateAction(ActionSet, &Info, &VibrationAction));

	if (HMD)
	{
		DeviceId = HMD->AddActionDevice(Action);
	}
}

FOpenXRInputPlugin::FOpenXRInput::FOpenXRInput(FOpenXRHMD* HMD)
	: OpenXRHMD(HMD)
	, ActionSets()
	, Actions()
	, Controllers()
	, bActionsBound(false)
	, MessageHandler(new FGenericApplicationMessageHandler())
{
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	check(OpenXRHMD);

	InitActions();
}

FOpenXRInputPlugin::FOpenXRInput::~FOpenXRInput()
{
	DestroyActions();
}

void FOpenXRInputPlugin::FOpenXRInput::InitActions()
{
	if (bActionsBound)
		return;

	XrInstance Instance = OpenXRHMD->GetInstance();
	check(Instance);
	DestroyActions();

	XrActionSet ActionSet = XR_NULL_HANDLE;
	XrActionSetCreateInfo SetInfo;
	SetInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	SetInfo.next = nullptr;
	FCStringAnsi::Strcpy(SetInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "ue4");
	FCStringAnsi::Strcpy(SetInfo.localizedActionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "Unreal Engine 4");
	XR_ENSURE(xrCreateActionSet(Instance, &SetInfo, &ActionSet));

	const UInputSettings* InputSettings = GetDefault<UInputSettings>();
	if (InputSettings != nullptr)
	{
		TArray<FName> ActionNames;
		InputSettings->GetActionNames(ActionNames);
		for (const auto& ActionName : ActionNames)
		{
			TArray<FInputActionKeyMapping> Mappings;
			InputSettings->GetActionMappingByName(ActionName, Mappings);
			Actions.Emplace(ActionSet, Mappings);
		}

		TArray<FName> AxisNames;
		InputSettings->GetAxisNames(AxisNames);
		for (const auto& AxisName : AxisNames)
		{
			TArray<FInputAxisKeyMapping> Mappings;
			InputSettings->GetAxisMappingByName(AxisName, Mappings);
			Actions.Emplace(ActionSet, Mappings);
		}
	}

	const UXRInputSettings* XRInputSettings = GetDefault<UXRInputSettings>();

	// Controller poses
	Controllers.Add(EControllerHand::Left, FOpenXRController(OpenXRHMD, ActionSet, "Left Controller"));
	Controllers.Add(EControllerHand::Right, FOpenXRController(OpenXRHMD, ActionSet, "Right Controller"));

	SuggestedBindings(Instance, "/interaction_profiles/khr/simple_controller", XRInputSettings->SimpleController, XRInputSettings->SimpleBindings);
	SuggestedBindings(Instance, "/interaction_profiles/google/daydream_controller", XRInputSettings->DaydreamController, XRInputSettings->DaydreamBindings);
	SuggestedBindings(Instance, "/interaction_profiles/htc/vive_controller", XRInputSettings->ViveController, XRInputSettings->ViveBindings);
	SuggestedBindings(Instance, "/interaction_profiles/htc/vive_pro", XRInputSettings->ViveProHeadset, XRInputSettings->ViveProBindings);
	SuggestedBindings(Instance, "/interaction_profiles/microsoft/motion_controller", XRInputSettings->MixedRealityController, XRInputSettings->MixedRealityBindings);
	SuggestedBindings(Instance, "/interaction_profiles/oculus/go_controller", XRInputSettings->OculusGoController, XRInputSettings->OculusGoBindings);
	SuggestedBindings(Instance, "/interaction_profiles/oculus/touch_controller", XRInputSettings->OculusTouchController, XRInputSettings->OculusTouchBindings);
	SuggestedBindings(Instance, "/interaction_profiles/valve/index_controller", XRInputSettings->ValveIndexController, XRInputSettings->ValveIndexBindings);

	XrActiveActionSet ActiveSet;
	ActiveSet.actionSet = ActionSet;
	ActiveSet.subactionPath = XR_NULL_PATH;
	ActionSets.Add(ActiveSet);
}

void FOpenXRInputPlugin::FOpenXRInput::DestroyActions()
{
	for (XrActiveActionSet& ActionSet : ActionSets)
	{
		XR_ENSURE(xrDestroyActionSet(ActionSet.actionSet));
	}
	ActionSets.Empty();
	Actions.Empty();
	Controllers.Empty();
}

void FOpenXRInputPlugin::FOpenXRInput::SuggestedBindings(XrInstance Instance, const char* Profile, const FXRInteractionProfileSettings& Settings, const TArray<FXRSuggestedBinding>& SuggestedBindings)
{
	if (!Settings.Supported)
	{
		return;
	}

	TArray<XrActionSuggestedBinding> Bindings;
	for (const auto& Suggestion : SuggestedBindings)
	{
		if (Suggestion.Path.IsEmpty())
		{
			continue;
		}

		const FKey& Key = Suggestion.Key;
		const FOpenXRAction* Action = Actions.FindByPredicate([Key](FOpenXRAction& Mapping)
		{
			int32 Index;
			if (Mapping.Keys.Find(Key, Index))
			{
				Mapping.Keys.Swap(0, Index);
				return true;
			}
			return false;
		});

		if (Action)
		{
			XrActionSuggestedBinding Binding;
			Binding.action = Action->Handle;
			Binding.binding = GetPath(Instance, Suggestion.Path);
			Bindings.Add(Binding);
		}
	}

	if (Settings.HasControllers)
	{
		XrActionSuggestedBinding LeftBinding, RightBinding;
		LeftBinding.action = Controllers[EControllerHand::Left].Action;
		RightBinding.action = Controllers[EControllerHand::Right].Action;

		if (Settings.ControllerPose == AimPose)
		{
			LeftBinding.binding = GetPath(Instance, "/user/hand/left/input/aim/pose");
			RightBinding.binding = GetPath(Instance, "/user/hand/right/input/aim/pose");
		}
		else
		{
			LeftBinding.binding = GetPath(Instance, "/user/hand/left/input/grip/pose");
			RightBinding.binding = GetPath(Instance, "/user/hand/right/input/grip/pose");
		}

		Bindings.Add(LeftBinding);
		Bindings.Add(RightBinding);

		if (Settings.HasHaptics)
		{
			Bindings.Add(XrActionSuggestedBinding{ Controllers[EControllerHand::Left].VibrationAction, GetPath(Instance, "/user/hand/left/output/haptic") });
			Bindings.Add(XrActionSuggestedBinding{ Controllers[EControllerHand::Right].VibrationAction, GetPath(Instance, "/user/hand/right/output/haptic") });
		}
	}

	XrInteractionProfileSuggestedBinding InteractionProfile;
	InteractionProfile.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	InteractionProfile.next = nullptr;
	InteractionProfile.interactionProfile = GetPath(Instance, Profile);
	InteractionProfile.countSuggestedBindings = Bindings.Num();
	InteractionProfile.suggestedBindings = Bindings.GetData();
	XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &InteractionProfile));
}

void FOpenXRInputPlugin::FOpenXRInput::Tick(float DeltaTime)
{
	XrSession Session = OpenXRHMD->GetSession();

	if (OpenXRHMD->IsRunning())
	{
		if (!bActionsBound)
		{
			TArray<XrActionSet> BindActionSets;
			for (auto && BindActionSet : ActionSets)
				BindActionSets.Add(BindActionSet.actionSet);

			XrSessionActionSetsAttachInfo SessionActionSetsAttachInfo;
			SessionActionSetsAttachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
			SessionActionSetsAttachInfo.next = nullptr;
			SessionActionSetsAttachInfo.countActionSets = BindActionSets.Num();
			SessionActionSetsAttachInfo.actionSets = BindActionSets.GetData();
			XR_ENSURE(xrAttachSessionActionSets(Session, &SessionActionSetsAttachInfo));

			bActionsBound = true;
		}
	}
	else if (bActionsBound)
	{
		// If the session shut down, clean up.
		bActionsBound = false;
	}

	if (OpenXRHMD->IsFocused())
	{
		XrActionsSyncInfo SyncInfo;
		SyncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
		SyncInfo.next = nullptr;
		SyncInfo.countActiveActionSets = ActionSets.Num();
		SyncInfo.activeActionSets = ActionSets.GetData();
		XR_ENSURE(xrSyncActions(Session, &SyncInfo));
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SendControllerEvents()
{
	if (!bActionsBound)
	{
		return;
	}

	XrSession Session = OpenXRHMD->GetSession();

	for (auto& Action : Actions)
	{
		XrActionStateGetInfo GetInfo;
		GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
		GetInfo.next = nullptr;
		GetInfo.subactionPath = XR_NULL_PATH;
		GetInfo.action = Action.Handle;

		switch (Action.Type)
		{
		case XR_ACTION_TYPE_BOOLEAN_INPUT:
		{
			XrActionStateBoolean State;
			State.type = XR_TYPE_ACTION_STATE_BOOLEAN;
			State.next = nullptr;
			XrResult Result = xrGetActionStateBoolean(Session, &GetInfo, &State);

			if (Result >= XR_SUCCESS && State.changedSinceLastSync)
			{
				if (State.currentState)
				{
					MessageHandler->OnControllerButtonPressed(Action.Keys[0].GetFName(), 0, /*IsRepeat =*/false);
				}
				else
				{
					MessageHandler->OnControllerButtonReleased(Action.Keys[0].GetFName(), 0, /*IsRepeat =*/false);
				}
			}
		}
		break;
		case XR_ACTION_TYPE_FLOAT_INPUT:
		{
			XrActionStateFloat State;
			State.type = XR_TYPE_ACTION_STATE_FLOAT;
			State.next = nullptr;
			XrResult Result = xrGetActionStateFloat(Session, &GetInfo, &State);
			if (Result >= XR_SUCCESS && State.changedSinceLastSync)
			{
				MessageHandler->OnControllerAnalog(Action.Keys[0].GetFName(), 0, State.currentState);
			}
		}
		break;
		default:
			// Other action types are currently unsupported.
			break;
		}
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;

	UXRInputSettings::OnSuggestedBindingsChanged.AddSP(this, &FOpenXRInputPlugin::FOpenXRInput::InitActions);
#if WITH_EDITOR
	FEditorDelegates::OnActionAxisMappingsChanged.AddSP(this, &FOpenXRInputPlugin::FOpenXRInput::InitActions);
#endif
}

bool FOpenXRInputPlugin::FOpenXRInput::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return false;
}

void FOpenXRInputPlugin::FOpenXRInput::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
	// Large channel type maps to amplitude. We are interested in amplitude.
	if ((ChannelType == FForceFeedbackChannelType::LEFT_LARGE) ||
		(ChannelType == FForceFeedbackChannelType::RIGHT_LARGE))
	{
		FHapticFeedbackValues Values(XR_FREQUENCY_UNSPECIFIED, Value);
		SetHapticFeedbackValues(ControllerId, ChannelType == FForceFeedbackChannelType::LEFT_LARGE ? (int32)EControllerHand::Left : (int32)EControllerHand::Right, Values);
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values)
{
	FHapticFeedbackValues leftHaptics = FHapticFeedbackValues(
		values.LeftSmall,		// frequency
		values.LeftLarge);		// amplitude
	FHapticFeedbackValues rightHaptics = FHapticFeedbackValues(
		values.RightSmall,		// frequency
		values.RightLarge);		// amplitude

	SetHapticFeedbackValues(
		ControllerId,
		(int32)EControllerHand::Left,
		leftHaptics);

	SetHapticFeedbackValues(
		ControllerId,
		(int32)EControllerHand::Right,
		rightHaptics);
}

FName FOpenXRInputPlugin::FOpenXRInput::GetMotionControllerDeviceTypeName() const
{
	return FName(TEXT("OpenXR"));
}
bool FOpenXRInputPlugin::FOpenXRInput::GetControllerOrientationAndPosition(const int32 ControllerIndex, const EControllerHand DeviceHand, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	if (ControllerIndex == 0 && (DeviceHand == EControllerHand::Left || DeviceHand == EControllerHand::Right))
	{
		FQuat Orientation;
		OpenXRHMD->GetCurrentPose(Controllers[DeviceHand].DeviceId, Orientation, OutPosition);
		OutOrientation = FRotator(Orientation);
		return true;
	}

	return false;
}

ETrackingStatus FOpenXRInputPlugin::FOpenXRInput::GetControllerTrackingStatus(const int32 ControllerIndex, const EControllerHand DeviceHand) const
{
	if (ControllerIndex == 0 && (DeviceHand == EControllerHand::Left || DeviceHand == EControllerHand::Right || DeviceHand == EControllerHand::AnyHand))
	{
		return ETrackingStatus::Tracked;
	}

	return ETrackingStatus::NotTracked;
}

// TODO: Refactor API to change the Hand type to EControllerHand
void FOpenXRInputPlugin::FOpenXRInput::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values)
{
	XrSession Session = OpenXRHMD->GetSession();

	XrHapticVibration HapticValue;
	HapticValue.type = XR_TYPE_HAPTIC_VIBRATION;
	HapticValue.next = nullptr;
	HapticValue.duration = MaxFeedbackDuration;
	HapticValue.frequency = Values.Frequency;
	HapticValue.amplitude = Values.Amplitude;

	if (ControllerId == 0)
	{
		if (Hand == (int32)EControllerHand::Left || Hand == (int32)EControllerHand::AnyHand)
		{
			XrHapticActionInfo HapticActionInfo;
			HapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
			HapticActionInfo.next = nullptr;
			HapticActionInfo.subactionPath = XR_NULL_PATH;
			HapticActionInfo.action = Controllers[EControllerHand::Left].VibrationAction;
			if (Values.Amplitude <= 0.0f || Values.Frequency < XR_FREQUENCY_UNSPECIFIED)
			{
				XR_ENSURE(xrStopHapticFeedback(Session, &HapticActionInfo));
			}
			else
			{
				XR_ENSURE(xrApplyHapticFeedback(Session, &HapticActionInfo, (const XrHapticBaseHeader*)&HapticValue));
			}
		}
		if (Hand == (int32)EControllerHand::Right || Hand == (int32)EControllerHand::AnyHand)
		{
			XrHapticActionInfo HapticActionInfo;
			HapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
			HapticActionInfo.next = nullptr;
			HapticActionInfo.subactionPath = XR_NULL_PATH;
			HapticActionInfo.action = Controllers[EControllerHand::Right].VibrationAction;
			if (Values.Amplitude <= 0.0f || Values.Frequency < XR_FREQUENCY_UNSPECIFIED)
			{
				XR_ENSURE(xrStopHapticFeedback(Session, &HapticActionInfo));
			}
			else
			{
				XR_ENSURE(xrApplyHapticFeedback(Session, &HapticActionInfo, (const XrHapticBaseHeader*)&HapticValue));
			}
		}
	}
}

void FOpenXRInputPlugin::FOpenXRInput::GetHapticFrequencyRange(float& MinFrequency, float& MaxFrequency) const
{
	MinFrequency = XR_FREQUENCY_UNSPECIFIED;
	MaxFrequency = XR_FREQUENCY_UNSPECIFIED;
}

float FOpenXRInputPlugin::FOpenXRInput::GetHapticAmplitudeScale() const
{
	return 1.0f;
}

#undef LOCTEXT_NAMESPACE
