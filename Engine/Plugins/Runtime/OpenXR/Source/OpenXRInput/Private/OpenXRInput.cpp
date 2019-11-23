// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "OpenXRInput.h"
#include "OpenXRHMD.h"
#include "OpenXRHMDPrivate.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/InputSettings.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#endif

#include <openxr/openxr.h>

FORCEINLINE XrPath GetPath(XrInstance Instance, const char* PathString)
{
	XrPath Path = XR_NULL_PATH;
	XrResult Result = xrStringToPath(Instance, PathString, &Path);
	check(XR_SUCCEEDED(Result));
	return Path;
}

FORCEINLINE XrPath GetPath(XrInstance Instance, const FString& PathString)
{
	return GetPath(Instance, (ANSICHAR*)StringCast<ANSICHAR>(*PathString).Get());
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
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InActionSet, XrActionType InActionType, const FName& InName, const TArray<XrPath>& SubactionPaths)
	: Set(InActionSet)
	, Type(InActionType)
	, Name(InName)
	, Handle(XR_NULL_HANDLE)
	, KeyMap()
{
	char ActionName[NAME_SIZE];
	Name.GetPlainANSIString(ActionName);

	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	FilterActionName(ActionName, Info.actionName);
	Info.actionType = Type;
	Info.countSubactionPaths = SubactionPaths.Num();
	Info.subactionPaths = SubactionPaths.GetData();
	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, ActionName);
	XR_ENSURE(xrCreateAction(Set, &Info, &Handle));
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

FOpenXRInputPlugin::FInteractionProfile::FInteractionProfile(XrPath InProfile)
	: Path(InProfile)
	, Bindings()
{
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

	BuildActions();
}

FOpenXRInputPlugin::FOpenXRInput::~FOpenXRInput()
{
	DestroyActions();
}

void FOpenXRInputPlugin::FOpenXRInput::BuildActions()
{
	if (bActionsBound)
	{
		return;
	}

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

	// Controller poses
	OpenXRHMD->ResetActionDevices();
	Controllers.Add(EControllerHand::Left, FOpenXRController(OpenXRHMD, ActionSet, "Left Controller"));
	Controllers.Add(EControllerHand::Right, FOpenXRController(OpenXRHMD, ActionSet, "Right Controller"));

	// Generate a map of all supported interaction profiles
	TMap<FString, FInteractionProfile> Profiles;
	Profiles.Add("Daydream", FInteractionProfile(GetPath(Instance, "/interaction_profiles/google/daydream_controller")));
	Profiles.Add("Vive", FInteractionProfile(GetPath(Instance, "/interaction_profiles/htc/vive_controller")));
	Profiles.Add("MixedReality", FInteractionProfile(GetPath(Instance, "/interaction_profiles/microsoft/motion_controller")));
	Profiles.Add("OculusGo", FInteractionProfile(GetPath(Instance, "/interaction_profiles/oculus/go_controller")));
	Profiles.Add("OculusTouch", FInteractionProfile(GetPath(Instance, "/interaction_profiles/oculus/touch_controller")));
	Profiles.Add("ValveIndex", FInteractionProfile(GetPath(Instance, "/interaction_profiles/valve/index_controller")));

	// Generate a list of the sub-action paths so we can query the left/right hand individually
	SubactionPaths.Add(GetPath(Instance, "/user/hand/left"));
	SubactionPaths.Add(GetPath(Instance, "/user/hand/right"));

	auto InputSettings = GetMutableDefault<UInputSettings>();
	if (InputSettings != nullptr)
	{
		TArray<FName> ActionNames;
		InputSettings->GetActionNames(ActionNames);
		for (const FName& ActionName : ActionNames)
		{
			FOpenXRAction Action(ActionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, ActionName, SubactionPaths);
			TArray<FInputActionKeyMapping> Mappings;
			InputSettings->GetActionMappingByName(ActionName, Mappings);

			// If the developer didn't suggest any XR bindings for this action we won't expose it to the runtime
			if (SuggestBindings(Instance, Action, Mappings, Profiles) > 0)
			{
				Actions.Add(Action);
			}
			else
			{
				XR_ENSURE(xrDestroyAction(Action.Handle));
			}
		}

		TArray<FName> AxisNames;
		InputSettings->GetAxisNames(AxisNames);
		for (const FName& AxisName : AxisNames)
		{
			FOpenXRAction Action(ActionSet, XR_ACTION_TYPE_FLOAT_INPUT, AxisName, SubactionPaths);
			TArray<FInputAxisKeyMapping> Mappings;
			InputSettings->GetAxisMappingByName(AxisName, Mappings);

			// If the developer didn't suggest any XR bindings for this action we won't expose it to the runtime
			if (SuggestBindings(Instance, Action, Mappings, Profiles) > 0)
			{
				Actions.Add(Action);
			}
			else
			{
				XR_ENSURE(xrDestroyAction(Action.Handle));
			}
		}

		InputSettings->ForceRebuildKeymaps();
	}

	for (TPair<FString, FInteractionProfile>& Pair : Profiles)
	{
		FInteractionProfile& Profile = Pair.Value;

		// Only suggest interaction profile bindings if the developer has provided bindings for them
		if (Profile.Bindings.Num() > 0)
		{
			// Add the bindings for the controller pose and haptics
			Profile.Bindings.Add(XrActionSuggestedBinding {
				Controllers[EControllerHand::Left].Action, GetPath(Instance, "/user/hand/left/input/grip/pose")
			});
			Profile.Bindings.Add(XrActionSuggestedBinding {
				Controllers[EControllerHand::Right].Action, GetPath(Instance, "/user/hand/right/input/grip/pose")
			});
			Profile.Bindings.Add(XrActionSuggestedBinding {
				Controllers[EControllerHand::Left].VibrationAction, GetPath(Instance, "/user/hand/left/output/haptic")
			});
			Profile.Bindings.Add(XrActionSuggestedBinding {
				Controllers[EControllerHand::Right].VibrationAction, GetPath(Instance, "/user/hand/right/output/haptic")
			});

			XrInteractionProfileSuggestedBinding InteractionProfile;
			InteractionProfile.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
			InteractionProfile.next = nullptr;
			InteractionProfile.interactionProfile = Profile.Path;
			InteractionProfile.countSuggestedBindings = Profile.Bindings.Num();
			InteractionProfile.suggestedBindings = Profile.Bindings.GetData();
			XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &InteractionProfile));
		}
	}

	// Add an active set for each sub-action path so we can use the subaction paths later
	// TODO: Runtimes already allow us to do the same by simply specifying "subactionPath"
	// as XR_NULL_HANDLE, we're just being verbose for safety.
	for (XrPath Subaction : SubactionPaths)
	{
		XrActiveActionSet ActiveSet;
		ActiveSet.actionSet = ActionSet;
		ActiveSet.subactionPath = Subaction;
		ActionSets.Add(ActiveSet);
	}
}

void FOpenXRInputPlugin::FOpenXRInput::DestroyActions()
{
	// Destroying an action set will also destroy all actions in the set
	for (XrActiveActionSet& ActionSet : ActionSets)
	{
		xrDestroyActionSet(ActionSet.actionSet);
	}

	Actions.Reset();
	Controllers.Reset();
	SubactionPaths.Reset();
	ActionSets.Reset();
}

template<typename T>
int32 FOpenXRInputPlugin::FOpenXRInput::SuggestBindings(XrInstance Instance, FOpenXRAction& Action, const TArray<T>& Mappings, TMap<FString, FInteractionProfile>& Profiles)
{
	int32 SuggestedBindings = 0;

	// Add suggested bindings for every mapping
	for (const T& InputKey : Mappings)
	{
		// Key names that are parseable into an OpenXR path have exactly 4 tokens
		TArray<FString> Tokens;
		if (InputKey.Key.ToString().ParseIntoArray(Tokens, TEXT("_")) != 4)
		{
			continue;
		}

		// Check if we support the profile specified in the key name
		FInteractionProfile* Profile = Profiles.Find(Tokens[0]);
		if (Profile)
		{
			// Parse the key name into an OpenXR interaction profile path
			FString Path = "/user/hand/" + Tokens[1].ToLower();
			XrPath TopLevel = GetPath(Instance, Path);

			// Map this key to the correct subaction for this profile
			// We'll use this later to trigger the correct key
			TPair<XrPath, XrPath> Key(Profile->Path, TopLevel);
			Action.KeyMap.Add(Key, InputKey.Key.GetFName());

			// Add the input we want to query with grip being defined as "squeeze" in OpenXR
			FString Identifier = Tokens[2].ToLower();
			if (Identifier == "grip")
			{
				Identifier = "squeeze";
			}
			Path += "/input/" + Identifier;

			// Add the data we want to query, we'll skip this for trigger/squeeze "click" actions to allow
			// certain profiles that don't have "click" data to threshold the "value" data instead
			FString Component = Tokens[3].ToLower();
			if (Component == "axis")
			{
				Path += "/value";
			}
			else if (Component == "click")
			{
				if (Identifier != "trigger" && Identifier != "squeeze")
				{
					Path += "/click";
				}
			}
			else if (Component == "touch" || Component == "x" || Component == "y")
			{
				Path += "/" + Component;
			}
			else
			{
				// Unrecognized data
				continue;
			}

			// Add the binding to the profile
			Profile->Bindings.Add(XrActionSuggestedBinding{ Action.Handle, GetPath(Instance, Path) });
			SuggestedBindings++;
		}
	}

	return SuggestedBindings;
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

	for (XrPath Subaction : SubactionPaths)
	{
		XrInteractionProfileState Profile;
		Profile.type = XR_TYPE_INTERACTION_PROFILE_STATE;
		Profile.next = nullptr;
		XR_ENSURE(xrGetCurrentInteractionProfile(Session, Subaction, &Profile));

		TPair<XrPath, XrPath> Key(Profile.interactionProfile, Subaction);
		for (FOpenXRAction& Action : Actions)
		{
			XrActionStateGetInfo GetInfo;
			GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
			GetInfo.next = nullptr;
			GetInfo.subactionPath = Subaction;
			GetInfo.action = Action.Handle;

			FName* ActionKey = Action.KeyMap.Find(Key);
			if (!ActionKey)
			{
				continue;
			}

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
						MessageHandler->OnControllerButtonPressed(*ActionKey, 0, /*IsRepeat =*/false);
					}
					else
					{
						MessageHandler->OnControllerButtonReleased(*ActionKey, 0, /*IsRepeat =*/false);
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
					MessageHandler->OnControllerAnalog(*ActionKey, 0, State.currentState);
				}
			}
			break;
			default:
				// Other action types are currently unsupported.
				break;
			}
		}
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;
#if WITH_EDITOR
	FEditorDelegates::OnActionAxisMappingsChanged.AddSP(this, &FOpenXRInputPlugin::FOpenXRInput::BuildActions);
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
