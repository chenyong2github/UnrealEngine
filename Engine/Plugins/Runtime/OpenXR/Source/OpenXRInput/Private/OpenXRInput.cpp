// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRInput.h"
#include "OpenXRInputSettings.h"
#include "OpenXRHMD.h"
#include "OpenXRCore.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/InputSettings.h"
#include "IOpenXRExtensionPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"

#include "EnhancedInputModule.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableInputConfig.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"

#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#include "Editor.h"
#endif

#include <openxr/openxr.h>

#define LOCTEXT_NAMESPACE "OpenXRInputPlugin"

namespace OpenXRSourceNames
{
	static const FName AnyHand("AnyHand");
	static const FName Left("Left");
	static const FName Right("Right");
	static const FName LeftGrip("LeftGrip");
	static const FName RightGrip("RightGrip");
	static const FName LeftAim("LeftAim");
	static const FName RightAim("RightAim");
}

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
	static_assert(XR_MAX_ACTION_NAME_SIZE == XR_MAX_ACTION_SET_NAME_SIZE);

	// Ensure the action name is a well-formed path
	size_t i;
	for (i = 0; i < XR_MAX_ACTION_NAME_SIZE - 1 && InActionName[i] != '\0'; i++)
	{
		unsigned char c = InActionName[i];
		OutActionName[i] = (c == ' ') ? '-' : isalnum(c) ? tolower(c) : '_';
	}
	OutActionName[i] = '\0';
}

FORCEINLINE XrActionType ToActionType(EInputActionValueType ValueType)
{
	switch (ValueType)
	{
		case EInputActionValueType::Boolean: return XR_ACTION_TYPE_BOOLEAN_INPUT;
		case EInputActionValueType::Axis1D: return XR_ACTION_TYPE_FLOAT_INPUT;
		case EInputActionValueType::Axis2D: return XR_ACTION_TYPE_VECTOR2F_INPUT;
		case EInputActionValueType::Axis3D:
			// TODO: Add 3D vector support to OpenXR Input
			ensure(false);
			return XR_ACTION_TYPE_VECTOR2F_INPUT;
	}
	ensure(false);
	return (XrActionType)0;
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
	// Note: OpenXRHMD may be null, for example in the editor.  But we still need the input device to enumerate sources.
	InputDevice = MakeShared<FOpenXRInput>(OpenXRHMD);
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InActionSet,
	XrActionType InActionType, const FName& InName, const FString& InLocalizedName,
	const TArray<XrPath>& InSubactionPaths, const TObjectPtr<const UInputAction>& InObject)
	: FOpenXRAction(InActionSet, InActionType, InName, InLocalizedName, InSubactionPaths)
{
	Object = InObject;
}

FOpenXRInputPlugin::FOpenXRAction::FOpenXRAction(XrActionSet InActionSet,
	XrActionType InActionType, const FName& InName, const FString& InLocalizedName,
	const TArray<XrPath>& InSubactionPaths)
	: Set(InActionSet)
	, Type(InActionType)
	, Name(InName)
	, Handle(XR_NULL_HANDLE)
	, Object()
	, KeyMap()
{
	char ActionName[NAME_SIZE];
	Name.GetPlainANSIString(ActionName);

	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	FilterActionName(ActionName, Info.actionName);
	Info.actionType = Type;
	Info.countSubactionPaths = InSubactionPaths.Num();
	Info.subactionPaths = InSubactionPaths.GetData();
	if (!InLocalizedName.IsEmpty())
	{
		FTCHARToUTF8_Convert::Convert(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, *InLocalizedName, InLocalizedName.Len() + 1);
	}
	else
	{
		FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE, ActionName);
	}

	XR_ENSURE(xrCreateAction(Set, &Info, &Handle));
}

FOpenXRInputPlugin::FOpenXRActionSet::FOpenXRActionSet(XrInstance InInstance,
	const FName& InName, const FString& InLocalizedName, uint32 InPriority,
	const TObjectPtr<const UInputMappingContext>& InObject)
	: FOpenXRActionSet(InInstance, InName, InLocalizedName, InPriority)
{
	Object = InObject;
}

FOpenXRInputPlugin::FOpenXRActionSet::FOpenXRActionSet(XrInstance InInstance,
	const FName& InName, const FString& InLocalizedName, uint32 InPriority)
	: Handle(XR_NULL_HANDLE)
	, Name(InName)
	, LocalizedName(InLocalizedName)
	, Object()
{
	char ActionName[NAME_SIZE];
	Name.GetPlainANSIString(ActionName);

	XrActionSet ActionSet = XR_NULL_HANDLE;
	XrActionSetCreateInfo Info;
	Info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	Info.next = nullptr;
	FilterActionName(ActionName, Info.actionSetName);
	if (!InLocalizedName.IsEmpty())
	{
		FTCHARToUTF8_Convert::Convert(Info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, *InLocalizedName, InLocalizedName.Len() + 1);
	}
	else
	{
		FCStringAnsi::Strcpy(Info.localizedActionSetName, XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, ActionName);
	}
	Info.priority = InPriority;
	XR_ENSURE(xrCreateActionSet(InInstance, &Info, &Handle));
}

FOpenXRInputPlugin::FOpenXRController::FOpenXRController(XrActionSet InActionSet, XrPath InUserPath, const char* InName)
	: ActionSet(InActionSet)
	, UserPath(InUserPath)
	, GripAction(XR_NULL_HANDLE)
	, AimAction(XR_NULL_HANDLE)
	, VibrationAction(XR_NULL_HANDLE)
	, GripDeviceId(-1)
	, AimDeviceId(-1)
{
	XrActionCreateInfo Info;
	Info.type = XR_TYPE_ACTION_CREATE_INFO;
	Info.next = nullptr;
	Info.countSubactionPaths = 0;
	Info.subactionPaths = nullptr;

	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, InName);
	FCStringAnsi::Strcat(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, " Grip Pose");
	FilterActionName(Info.localizedActionName, Info.actionName);
	Info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	XR_ENSURE(xrCreateAction(ActionSet, &Info, &GripAction));

	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, InName);
	FCStringAnsi::Strcat(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, " Aim Pose");
	FilterActionName(Info.localizedActionName, Info.actionName);
	Info.actionType = XR_ACTION_TYPE_POSE_INPUT;
	XR_ENSURE(xrCreateAction(ActionSet, &Info, &AimAction));

	FCStringAnsi::Strcpy(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, InName);
	FCStringAnsi::Strcat(Info.localizedActionName, XR_MAX_ACTION_NAME_SIZE, " Vibration");
	FilterActionName(Info.localizedActionName, Info.actionName);
	Info.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
	XR_ENSURE(xrCreateAction(ActionSet, &Info, &VibrationAction));
}

void FOpenXRInputPlugin::FOpenXRController::AddActionDevices(FOpenXRHMD* HMD)
{
	if (HMD)
	{
		GripDeviceId = HMD->AddActionDevice(GripAction, UserPath);
		AimDeviceId = HMD->AddActionDevice(AimAction, UserPath);
	}
}

FOpenXRInputPlugin::FInteractionProfile::FInteractionProfile(XrPath InProfile, bool InHasHaptics)
	: HasHaptics(InHasHaptics)
	, Path(InProfile)
	, Bindings()
{
}

FOpenXRInputPlugin::FOpenXRInput::FOpenXRInput(FOpenXRHMD* HMD)
	: OpenXRHMD(HMD)
	, Profiles()
	, ActionSets()
	, PluginActionSets()
	, SubactionPaths()
	, LegacyActions()
	, EnhancedActions()
	, Controllers()
	, MotionSourceToControllerHandMap()
	, ControllerSet()
	, LegacySet()
	, MappableInputConfig(nullptr)
	, bActionsBound(false)
	, bDirectionalBindingSupported(false)
	, MessageHandler(new FGenericApplicationMessageHandler())
{
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
	
	// If there is no HMD then this module is not active, but it still needs to exist so we can EnumerateMotionSources from it.
	if (OpenXRHMD)
	{
		bDirectionalBindingSupported = OpenXRHMD->IsExtensionEnabled("XR_EXT_dpad_binding");

		// Note: AnyHand needs special handling because it tries left then falls back to right in each call.
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::Left, EControllerHand::Left);
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::Right, EControllerHand::Right);
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::LeftGrip, EControllerHand::Left);
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::RightGrip, EControllerHand::Right);
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::LeftAim, EControllerHand::Left);
		MotionSourceToControllerHandMap.Add(OpenXRSourceNames::RightAim, EControllerHand::Right);

		// Map the legacy hand enum values that openxr supports
		MotionSourceToControllerHandMap.Add(TEXT("EControllerHand::Left"), EControllerHand::Left);
		MotionSourceToControllerHandMap.Add(TEXT("EControllerHand::Right"), EControllerHand::Right);
		MotionSourceToControllerHandMap.Add(TEXT("EControllerHand::AnyHand"), EControllerHand::AnyHand);

		BuildActions();
	}
}

XrAction FOpenXRInputPlugin::FOpenXRInput::GetActionForMotionSource(FName MotionSource) const
{
	const EControllerHand* Hand = MotionSourceToControllerHandMap.Find(MotionSource);
	if (Hand == nullptr)
	{
		return XR_NULL_HANDLE;
	}
	const FOpenXRController& Controller = Controllers[*Hand];
	if (MotionSource == OpenXRSourceNames::LeftAim || MotionSource == OpenXRSourceNames::RightAim)
	{
		return Controller.AimAction;
	}
	else
	{
		return Controller.GripAction;
	}
}

int32 FOpenXRInputPlugin::FOpenXRInput::GetDeviceIDForMotionSource(FName MotionSource) const
{
	const FOpenXRController& Controller = Controllers[MotionSourceToControllerHandMap.FindChecked(MotionSource)];
	if (MotionSource == OpenXRSourceNames::LeftAim || MotionSource == OpenXRSourceNames::RightAim)
	{
		return Controller.AimDeviceId;
	}
	else
	{
		return Controller.GripDeviceId;
	}
}

XrPath FOpenXRInputPlugin::FOpenXRInput::GetUserPathForMotionSource(FName MotionSource) const
{
	const FOpenXRController& Controller = Controllers[MotionSourceToControllerHandMap.FindChecked(MotionSource)];
	return Controller.UserPath;
}

bool FOpenXRInputPlugin::FOpenXRInput::IsOpenXRInputSupportedMotionSource(const FName MotionSource) const
{
	return
		MotionSource == OpenXRSourceNames::AnyHand
		|| MotionSourceToControllerHandMap.Contains(MotionSource);
}

FOpenXRInputPlugin::FOpenXRInput::~FOpenXRInput()
{
	DestroyActions();
}

void FOpenXRInputPlugin::FOpenXRInput::BuildActions()
{
	if ((bActionsBound) || (OpenXRHMD == nullptr))
	{
		return;
	}

	XrInstance Instance = OpenXRHMD->GetInstance();
	check(Instance);
	DestroyActions();

	// Generate a map of all supported interaction profiles
	XrPath SimpleControllerPath = GetPath(Instance, "/interaction_profiles/khr/simple_controller");
	Profiles.Add("SimpleController", FInteractionProfile(SimpleControllerPath, true));
	Profiles.Add("Vive", FInteractionProfile(GetPath(Instance, "/interaction_profiles/htc/vive_controller"), true));
	Profiles.Add("MixedReality", FInteractionProfile(GetPath(Instance, "/interaction_profiles/microsoft/motion_controller"), true));
	Profiles.Add("OculusGo", FInteractionProfile(GetPath(Instance, "/interaction_profiles/oculus/go_controller"), false));
	Profiles.Add("OculusTouch", FInteractionProfile(GetPath(Instance, "/interaction_profiles/oculus/touch_controller"), true));
	Profiles.Add("ValveIndex", FInteractionProfile(GetPath(Instance, "/interaction_profiles/valve/index_controller"), true));

	// Query extension plugins for interaction profiles
	for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
	{
		FString KeyPrefix;
		XrPath Path = XR_NULL_PATH;
		bool HasHaptics = false;
		if (Plugin->GetInteractionProfile(Instance, KeyPrefix, Path, HasHaptics) && Path != XR_NULL_PATH)
		{
			Profiles.Add(KeyPrefix, FInteractionProfile(Path, HasHaptics));
		}
	}

	// Create an engine action set for pose input and haptic output
	FOpenXRActionSet ActionSet(Instance, "controllers", "Controllers", 0);
	ControllerSet.actionSet = ActionSet.Handle;
	ControllerSet.subactionPath = XR_NULL_PATH;

	XrPath LeftHand = GetPath(Instance, "/user/hand/left");
	XrPath RightHand = GetPath(Instance, "/user/hand/right");
	XrPath HMD = GetPath(Instance, "/user/head");

	// Controller poses
	OpenXRHMD->ResetActionDevices();
	Controllers.Add(EControllerHand::Left, FOpenXRController(ActionSet.Handle, LeftHand, "Left Controller"));
	Controllers.Add(EControllerHand::Right, FOpenXRController(ActionSet.Handle, RightHand, "Right Controller"));
	Controllers.Add(EControllerHand::HMD, FOpenXRController(ActionSet.Handle, HMD, "HMD"));
	ActionSets.Emplace(MoveTemp(ActionSet));

	// Generate a list of the sub-action paths so we can query the left/right hand individually
	SubactionPaths.Add(LeftHand);
	SubactionPaths.Add(RightHand);

	UOpenXRInputSettings* InputSettings = GetMutableDefault<UOpenXRInputSettings>();
	if (InputSettings && InputSettings->bUseEnhancedInput)
	{
		BuildEnhancedActions(InputSettings->MappableInputConfig);
	}
	else
	{
		BuildLegacyActions();
	}

	for (TPair<FString, FInteractionProfile>& Pair : Profiles)
	{
		FInteractionProfile& Profile = Pair.Value;

		// Only suggest interaction profile bindings if the developer has provided bindings for them
		// An exception is made for the Simple Controller Profile which is always bound as a fallback
		if (Profile.Bindings.Num() > 0 || Profile.Path == SimpleControllerPath)
		{
			// Add the bindings for the controller pose and haptics
			Profile.Bindings.Add(XrActionSuggestedBinding{
				Controllers[EControllerHand::Left].GripAction, GetPath(Instance, "/user/hand/left/input/grip/pose")
				});
			Profile.Bindings.Add(XrActionSuggestedBinding{
				Controllers[EControllerHand::Right].GripAction, GetPath(Instance, "/user/hand/right/input/grip/pose")
				});
			Profile.Bindings.Add(XrActionSuggestedBinding{
				Controllers[EControllerHand::Left].AimAction, GetPath(Instance, "/user/hand/left/input/aim/pose")
				});
			Profile.Bindings.Add(XrActionSuggestedBinding{
				Controllers[EControllerHand::Right].AimAction, GetPath(Instance, "/user/hand/right/input/aim/pose")
				});

			if (Profile.HasHaptics)
			{
				Profile.Bindings.Add(XrActionSuggestedBinding{
					Controllers[EControllerHand::Left].VibrationAction, GetPath(Instance, "/user/hand/left/output/haptic")
					});
				Profile.Bindings.Add(XrActionSuggestedBinding{
					Controllers[EControllerHand::Right].VibrationAction, GetPath(Instance, "/user/hand/right/output/haptic")
					});
			}

			XrInteractionProfileSuggestedBinding InteractionProfile;
			InteractionProfile.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
			InteractionProfile.next = nullptr;
			InteractionProfile.interactionProfile = Profile.Path;
			InteractionProfile.countSuggestedBindings = Profile.Bindings.Num();
			InteractionProfile.suggestedBindings = Profile.Bindings.GetData();

			XR_ENSURE(xrSuggestInteractionProfileBindings(Instance, &InteractionProfile));
		}
	}

	Controllers[EControllerHand::Left].AddActionDevices(OpenXRHMD);
	Controllers[EControllerHand::Right].AddActionDevices(OpenXRHMD);
}

void FOpenXRInputPlugin::FOpenXRInput::BuildLegacyActions()
{
	XrInstance Instance = OpenXRHMD->GetInstance();
	check(Instance);

	// Create an engine action set for pose input and haptic output
	FOpenXRActionSet ActionSet(Instance, "ue", "Unreal Engine", 0);
	LegacySet.actionSet = ActionSet.Handle;
	LegacySet.subactionPath = XR_NULL_PATH;

	auto InputSettings = GetMutableDefault<UInputSettings>();
	if (InputSettings != nullptr)
	{
		TArray<FName> ActionNames;
		InputSettings->GetActionNames(ActionNames);
		for (const FName& ActionName : ActionNames)
		{
			FOpenXRAction Action(ActionSet.Handle, XR_ACTION_TYPE_BOOLEAN_INPUT, ActionName, ActionName.ToString(), SubactionPaths);
			TArray<FInputActionKeyMapping> Mappings;
			InputSettings->GetActionMappingByName(ActionName, Mappings);

			// If the developer didn't suggest any XR bindings for this action we won't expose it to the runtime
			if (SuggestBindings(Instance, Action, Mappings) > 0)
			{
				LegacyActions.Add(Action);
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
			FOpenXRAction Action(ActionSet.Handle, XR_ACTION_TYPE_FLOAT_INPUT, AxisName, AxisName.ToString(), SubactionPaths);
			TArray<FInputAxisKeyMapping> Mappings;
			InputSettings->GetAxisMappingByName(AxisName, Mappings);

			// If the developer didn't suggest any XR bindings for this action we won't expose it to the runtime
			if (SuggestBindings(Instance, Action, Mappings) > 0)
			{
				LegacyActions.Add(Action);
			}
			else
			{
				XR_ENSURE(xrDestroyAction(Action.Handle));
			}
		}

		InputSettings->ForceRebuildKeymaps();
	}

	// Query extension plugins for actions
	for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS;
		Plugin->AddActions(Instance,
			[this, &ActionSet](XrActionType InActionType, const FName& InName, const TArray<XrPath>& InSubactionPaths)
			{
				// TODO?: Log deprecation warning at runtime, since overridden deprecated interface methods don't warn at compile time?
				FOpenXRAction Action(ActionSet.Handle, InActionType, InName, InName.ToString(), InSubactionPaths);
				LegacyActions.Add(Action);
				return Action.Handle;
			}
		);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS;
	}

	for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
	{
		IOpenXRExtensionPlugin::FCreateActionSetFunc CreateActionSetFunc =
			[this, Instance]
			(const IOpenXRExtensionPlugin::FActionSetParams& Params) -> XrActionSet
			{
				FOpenXRActionSet ActionSet(Instance, Params.Name, Params.LocalizedName.ToString(), Params.Priority);
				const XrActionSet ReturnHandle = ActionSet.Handle;
				PluginActionSets.Emplace(MoveTemp(ActionSet));
				return ReturnHandle;
			};

		IOpenXRExtensionPlugin::FCreateActionFunc CreateActionFunc =
			[this, Instance, ActionSet]
			(const IOpenXRExtensionPlugin::FActionParams& Params) -> XrAction
			{
				// TODO: Use Params.LocalizedName
				const XrActionSet UseActionSet = Params.Set == XR_NULL_HANDLE ? ActionSet.Handle : Params.Set;
				FOpenXRAction Action(UseActionSet, Params.Type, Params.Name, Params.LocalizedName.ToString(), Params.SubactionPaths);
				const XrAction ReturnHandle = Action.Handle;
				for (const FKey& Key : Params.SuggestedBindings)
				{
					SuggestBindingForKey(Instance, Action, Key);
				}
				LegacyActions.Emplace(MoveTemp(Action));
				return ReturnHandle;
			};

		Plugin->AddActions(Instance, CreateActionSetFunc, CreateActionFunc);
	}

	ActionSets.Emplace(MoveTemp(ActionSet));
}

void FOpenXRInputPlugin::FOpenXRInput::BuildEnhancedActions(const FSoftObjectPath& MappableInputConfigPath)
{
	XrInstance Instance = OpenXRHMD->GetInstance();
	check(Instance);

	MappableInputConfig = (UPlayerMappableInputConfig*)MappableInputConfigPath.TryLoad();
	if (!MappableInputConfig)
	{
		return;
	}
	MappableInputConfig->AddToRoot();

	for (const TPair<TObjectPtr<UInputMappingContext>, int32> MappingContext : MappableInputConfig->GetMappingContexts())
	{
		FOpenXRActionSet ActionSet(Instance, MappingContext.Key->GetFName(), MappingContext.Key->ContextDescription.ToString(), MappingContext.Value, MappingContext.Key);
		TMap<FName, int32> ActionMap;

		for (const FEnhancedActionKeyMapping& Mapping : MappingContext.Key->GetMappings())
		{
			if (!Mapping.Action)
			{
				continue;
			}

			// Try to find an existing action within the current action set
			FName ActionName = Mapping.Action->GetFName();
			int32& ActionIndex = ActionMap.FindOrAdd(ActionName, INDEX_NONE);
			if (ActionIndex == INDEX_NONE)
			{
				// No action found, create a new one
				FString LocalizedName = Mapping.Action->ActionDescription.ToString();
				XrActionType ActionType = ToActionType(Mapping.Action->ValueType);
				if (!ActionType)
				{
					continue;
				}

				// Create the action and write the index to the reference in the actions map
				ActionIndex = EnhancedActions.Emplace(ActionSet.Handle, ActionType, ActionName, LocalizedName, SubactionPaths, Mapping.Action);
			}

			SuggestBindingForKey(Instance, EnhancedActions[ActionIndex], Mapping.Key, Mapping.Modifiers, Mapping.Triggers);
		}
		ActionSets.Emplace(MoveTemp(ActionSet));
	}
}

void FOpenXRInputPlugin::FOpenXRInput::DestroyActions()
{
	// Destroying an action set will also destroy all actions in the set
	for (const FOpenXRActionSet& ActionSet : ActionSets)
	{
		xrDestroyActionSet(ActionSet.Handle);
	}

	// Clear the active controller and legacy action set
	ControllerSet.actionSet = XR_NULL_HANDLE;
	ControllerSet.subactionPath = XR_NULL_PATH;
	LegacySet.actionSet = XR_NULL_HANDLE;
	LegacySet.subactionPath = XR_NULL_PATH;

	Profiles.Reset();
	LegacyActions.Reset();
	EnhancedActions.Reset();
	Controllers.Reset();
	SubactionPaths.Reset();
	ActionSets.Reset();
	PluginActionSets.Reset();
}

template<typename T>
int32 FOpenXRInputPlugin::FOpenXRInput::SuggestBindings(XrInstance Instance, FOpenXRAction& Action, const TArray<T>& Mappings)
{
	int32 SuggestedBindings = 0;

	// Add suggested bindings for every mapping
	for (const T& InputKey : Mappings)
	{
		if (SuggestBindingForKey(Instance, Action, InputKey.Key))
		{
			++SuggestedBindings;
		}
	}

	return SuggestedBindings;
}

bool FOpenXRInputPlugin::FOpenXRInput::SuggestBindingForKey(XrInstance Instance, FOpenXRAction& Action, const FKey& InFKey, const TArray<UInputModifier*>& Modifiers, const TArray<UInputTrigger*>& Triggers)
{
	// Key names that are parseable into an OpenXR path have exactly 4 tokens
	TArray<FString> Tokens;
	if (InFKey.ToString().ParseIntoArray(Tokens, TEXT("_")) != EKeys::NUM_XR_KEY_TOKENS)
	{
		return false;
	}

	// Check if we support the profile specified in the key name
	FInteractionProfile* Profile = Profiles.Find(Tokens[0]);
	if (!Profile)
	{
		return false;
	}

	// Parse the key name into an OpenXR interaction profile path
	FString Path = "/user/hand/" + Tokens[1].ToLower();
	XrPath TopLevel = GetPath(Instance, Path);

	// Map this key to the correct subaction for this profile
	// We'll use this later to trigger the correct key
	TPair<XrPath, XrPath> Key(Profile->Path, TopLevel);
	Action.KeyMap.Add(Key, InFKey.GetFName());

	for (UInputTrigger* Trigger : Triggers)
	{
		TObjectPtr<UInputTrigger> Ptr = Trigger;
		Action.Triggers.AddUnique(Key, Ptr);
	}
	for (UInputModifier* Modifier : Modifiers)
	{
		TObjectPtr<UInputModifier> Ptr = Modifier;
		Action.Modifiers.AddUnique(Key, Ptr);
	}

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
		if (Tokens[0] == "ValveIndex" && (Identifier == "trackpad" || Identifier == "squeeze"))
		{
			return false;
		}

		// The OpenXR spec says that .../input/system/click might not be available for application usage
		if (Identifier == "system")
		{
			return false;
		}

		if (Identifier != "trigger" && Identifier != "squeeze")
		{
			Path += "/click";
		}
	}
	else if (Component == "touch")
	{
		Path += "/touch";
	}
	else if (Component == "touchaxis")
	{
		Path += "/touchvalue";  // Note: this is not a standard openxr identifier.  It is meant to represent some kind of analog touch sensor.
	}
	else if (Component == "up" || Component == "down" || Component == "left" || Component == "right")
	{
		if (!bDirectionalBindingSupported)
		{
			return false;
		}
		Path += "/dpad_" + Component;
	}
	else if (Component != "2d")
	{
		// Anything we don't need to translate can pass through
		// Except for 2D vectors, which don't need a component path
		Path += "/" + Component;
	}

	// Add the binding to the profile
	Profile->Bindings.Add(XrActionSuggestedBinding{ Action.Handle, GetPath(Instance, Path) });
	return true;
}

bool FOpenXRInputPlugin::FOpenXRInput::SuggestBindingForKey(XrInstance Instance, FOpenXRAction& Action, const FKey& InFKey)
{
	TArray<UInputModifier*> Modifiers;
	TArray<UInputTrigger*> Triggers;
	return SuggestBindingForKey(Instance, Action, InFKey, Modifiers, Triggers);
}

void FOpenXRInputPlugin::FOpenXRInput::Tick(float DeltaTime)
{
	if (OpenXRHMD == nullptr)
	{
		// In the editor, when we are not actually running OpenXR, but the IInputDevice exists so it can enumerate its motion sources.
		return;
	}

	XrSession Session = OpenXRHMD->GetSession();
	if (Session != XR_NULL_HANDLE)
	{
		if (!bActionsBound)
		{
			BuildActions();

			// Bind engine action sets
			TArray<XrActionSet> BindActionSets;
			for (auto && BindActionSet : ActionSets)
				BindActionSets.Add(BindActionSet.Handle);

			// Bind plugin action sets exposed via deprecated method
			for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
			{
				TArray<XrActiveActionSet> PluginAttachSets_Deprecated;
				PRAGMA_DISABLE_DEPRECATION_WARNINGS;
				Plugin->AddActionSets(PluginAttachSets_Deprecated);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS;
				for (const XrActiveActionSet& ActiveSet : PluginAttachSets_Deprecated)
				{
					// TODO?: Log deprecation warning at runtime, since overridden deprecated interface methods don't warn at compile time?
					BindActionSets.Add(ActiveSet.actionSet);
				}
			}

			for (auto&& BindActionSet : PluginActionSets)
				BindActionSets.Add(BindActionSet.Handle);

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

		// Remove the input config from the root
		if (MappableInputConfig)
		{
			MappableInputConfig->RemoveFromRoot();
			MappableInputConfig = nullptr;
		}
	}

	if (OpenXRHMD->IsFocused())
	{
		XrActionsSyncInfo SyncInfo;
		SyncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
		SyncInfo.next = nullptr;

		TArray<XrActiveActionSet> ActiveSets;
		for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
		{
			Plugin->GetActiveActionSetsForSync(ActiveSets);

			// TODO?: Log deprecation warning at runtime, since overridden deprecated interface methods don't warn at compile time?
			PRAGMA_DISABLE_DEPRECATION_WARNINGS;
			Plugin->AddActionSets(ActiveSets);
			PRAGMA_ENABLE_DEPRECATION_WARNINGS;

			SyncInfo.next = Plugin->OnSyncActions(Session, SyncInfo.next);
		}

		IEnhancedInputModule::Get().GetLibrary()->ForEachSubsystem([this, &ActiveSets](IEnhancedInputSubsystemInterface* Subsystem)
			{
				if (Subsystem)
				{
					for (const FOpenXRActionSet& ActionSet : ActionSets)
					{
						if (ActionSet.Object && Subsystem->HasMappingContext(ActionSet.Object))
						{
							XrActiveActionSet ActiveSet;
							ActiveSet.actionSet = ActionSet.Handle;
							ActiveSet.subactionPath = XR_NULL_PATH;
							ActiveSets.Add(ActiveSet);
						}
					}
				}
			});

		// The controller and legacy action sets are always active
		if (ControllerSet.actionSet)
		{
			ActiveSets.Add(ControllerSet);
		}
		if (LegacySet.actionSet)
		{
			ActiveSets.Add(LegacySet);
		}

		SyncInfo.countActiveActionSets = ActiveSets.Num();
		SyncInfo.activeActionSets = ActiveSets.GetData();

		XR_ENSURE(xrSyncActions(Session, &SyncInfo));

		for (IOpenXRExtensionPlugin* Plugin : OpenXRHMD->GetExtensionPlugins())
		{
			Plugin->PostSyncActions(Session);
		}
	}
}

namespace OpenXRInputNamespace
{
	FXRTimedInputActionDelegate* GetTimedInputActionDelegate(FName ActionName)
	{
		FXRTimedInputActionDelegate* XRTimedInputActionDelegate = UHeadMountedDisplayFunctionLibrary::OnXRTimedInputActionDelegateMap.Find(ActionName);
		if (XRTimedInputActionDelegate && !XRTimedInputActionDelegate->IsBound())
		{
			XRTimedInputActionDelegate = nullptr;
		}
		return XRTimedInputActionDelegate;
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SendControllerEvents()
{
	if (!bActionsBound || OpenXRHMD == nullptr)
	{
		return;
	}

	if (!OpenXRHMD->IsFocused())
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

		TSet<FName> ActivatedActions, ActivatedAxes;
		TPair<XrPath, XrPath> Key(Profile.interactionProfile, Subaction);
		for (FOpenXRAction& Action : LegacyActions)
		{
			XrActionStateGetInfo GetInfo;
			GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
			GetInfo.next = nullptr;
			GetInfo.subactionPath = Subaction;
			GetInfo.action = Action.Handle;

			// Find the action key and check if it has already been fired this frame.
			FName* ActionKey = Action.KeyMap.Find(Key);
			if (!ActionKey)
			{
				continue;
			}

			switch (Action.Type)
			{
			case XR_ACTION_TYPE_BOOLEAN_INPUT:
			if (!ActivatedActions.Contains(*ActionKey))
			{
				XrActionStateBoolean State;
				State.type = XR_TYPE_ACTION_STATE_BOOLEAN;
				State.next = nullptr;
				XrResult Result = xrGetActionStateBoolean(Session, &GetInfo, &State);
				IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
				if (XR_SUCCEEDED(Result) && State.changedSinceLastSync)
				{
					ActivatedActions.Add(*ActionKey);
					if (State.isActive && State.currentState)
					{
						// TODO: Map input devices here if OpenXR would like to
						MessageHandler->OnControllerButtonPressed(*ActionKey, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
					}
					else
					{
						// TODO: Map input devices here if OpenXR would like to
						MessageHandler->OnControllerButtonReleased(*ActionKey, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), /*IsRepeat =*/false);
					}

					FXRTimedInputActionDelegate* const Delegate = OpenXRInputNamespace::GetTimedInputActionDelegate(Action.Name);
					if (Delegate)
					{
						Delegate->Execute(State.currentState ? 1.0 : 0.0f, ToFTimespan(State.lastChangeTime));
					}
				}
			}
			break;
			case XR_ACTION_TYPE_FLOAT_INPUT:
			if (!ActivatedAxes.Contains(*ActionKey))
			{
				XrActionStateFloat State;
				State.type = XR_TYPE_ACTION_STATE_FLOAT;
				State.next = nullptr;
				XrResult Result = xrGetActionStateFloat(Session, &GetInfo, &State);
				IPlatformInputDeviceMapper& DeviceMapper = IPlatformInputDeviceMapper::Get();
				if (XR_SUCCEEDED(Result) && State.changedSinceLastSync)
				{
					ActivatedAxes.Add(*ActionKey);
					if (State.isActive)
					{
						// TODO: Map input devices here if OpenXR would like to
						MessageHandler->OnControllerAnalog(*ActionKey, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), State.currentState);
					}
					else
					{
						// TODO: Map input devices here if OpenXR would like to
						MessageHandler->OnControllerAnalog(*ActionKey, DeviceMapper.GetPrimaryPlatformUser(), DeviceMapper.GetDefaultInputDevice(), 0.0f);
					}

					FXRTimedInputActionDelegate* const Delegate = OpenXRInputNamespace::GetTimedInputActionDelegate(Action.Name);
					if (Delegate)
					{
						Delegate->Execute(State.currentState, ToFTimespan(State.lastChangeTime));
					}
				}
			}
			break;
			default:
				// Other action types are currently unsupported.
				break;
			}
		}

		for (FOpenXRAction& Action : EnhancedActions)
		{
			const UInputAction* InputAction = Action.Object;

			XrActionStateGetInfo GetInfo;
			GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
			GetInfo.next = nullptr;
			GetInfo.subactionPath = Subaction;
			GetInfo.action = Action.Handle;

			FInputActionValue InputValue;
			switch (Action.Type)
			{
			case XR_ACTION_TYPE_BOOLEAN_INPUT:
			{
				XrActionStateBoolean State;
				State.type = XR_TYPE_ACTION_STATE_BOOLEAN;
				State.next = nullptr;
				XrResult Result = xrGetActionStateBoolean(Session, &GetInfo, &State);
				if (XR_SUCCEEDED(Result))
				{
					InputValue = FInputActionValue(State.isActive ? (bool)State.currentState : false);
				}
				else
				{
					continue;
				}
			}
			break;
			case XR_ACTION_TYPE_FLOAT_INPUT:
			{
				XrActionStateFloat State;
				State.type = XR_TYPE_ACTION_STATE_FLOAT;
				State.next = nullptr;
				XrResult Result = xrGetActionStateFloat(Session, &GetInfo, &State);
				if (XR_SUCCEEDED(Result))
				{
					InputValue = FInputActionValue(State.isActive ? State.currentState : 0.0f);
				}
				else
				{
					continue;
				}
			}
			break;
			case XR_ACTION_TYPE_VECTOR2F_INPUT:
			{
				XrActionStateVector2f State;
				State.type = XR_TYPE_ACTION_STATE_VECTOR2F;
				State.next = nullptr;
				XrResult Result = xrGetActionStateVector2f(Session, &GetInfo, &State);
				if (XR_SUCCEEDED(Result))
				{
					InputValue = FInputActionValue(State.isActive ? ToFVector2D(State.currentState) : FVector2D::ZeroVector);
				}
				else
				{
					continue;
				}
			}
			break;
			default:
				// Other action types are currently unsupported.
				continue;
			}

			TArray<TObjectPtr<UInputTrigger>> Triggers;
			Action.Triggers.MultiFind(Key, Triggers, false);
			TArray<TObjectPtr<UInputModifier>> Modifiers;
			Action.Modifiers.MultiFind(Key, Modifiers, false);
			IEnhancedInputModule::Get().GetLibrary()->ForEachSubsystem([InputAction, InputValue, Triggers, Modifiers](IEnhancedInputSubsystemInterface* Subsystem)
				{
					if (Subsystem)
					{
						Subsystem->InjectInputForAction(InputAction, InputValue, Modifiers, Triggers);
					}
				});
		}
	}
}

void FOpenXRInputPlugin::FOpenXRInput::SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
{
	MessageHandler = InMessageHandler;
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

bool FOpenXRInputPlugin::FOpenXRInput::SupportsForceFeedback(int32 ControllerId)
{
	if (!bActionsBound || OpenXRHMD == nullptr)
	{
		return false;
	}

	XrSession Session = OpenXRHMD->GetSession();
	return Session != XR_NULL_HANDLE;
}

void FOpenXRInputPlugin::FOpenXRInput::SetDeviceProperty(int32 ControllerId, const FInputDeviceProperty* Property)
{
	if (OpenXRHMD == nullptr)
	{
		return;
	}

	for (IOpenXRExtensionPlugin* Module : OpenXRHMD->GetExtensionPlugins())
	{
		Module->OnSetDeviceProperty(OpenXRHMD->GetSession(), ControllerId, Property);
	}
}

FName FOpenXRInputPlugin::FOpenXRInput::GetMotionControllerDeviceTypeName() const
{
	return FName(TEXT("OpenXR"));
}


bool FOpenXRInputPlugin::FOpenXRInput::GetControllerOrientationAndPosition(const int32 ControllerIndex, const FName MotionSource, FRotator& OutOrientation, FVector& OutPosition, float WorldToMetersScale) const
{
	if (OpenXRHMD == nullptr)
	{
		return false;
	}

	if (ControllerIndex == 0 && IsOpenXRInputSupportedMotionSource(MotionSource))
	{
		if (MotionSource == OpenXRSourceNames::AnyHand)
		{
			return GetControllerOrientationAndPosition(ControllerIndex, OpenXRSourceNames::LeftGrip, OutOrientation, OutPosition, WorldToMetersScale)
				|| GetControllerOrientationAndPosition(ControllerIndex, OpenXRSourceNames::RightGrip, OutOrientation, OutPosition, WorldToMetersScale);
		}

		if (MotionSource == OpenXRSourceNames::Left)
		{
			return GetControllerOrientationAndPosition(ControllerIndex, OpenXRSourceNames::LeftGrip, OutOrientation, OutPosition, WorldToMetersScale);
		}

		if (MotionSource == OpenXRSourceNames::Right)
		{
			return GetControllerOrientationAndPosition(ControllerIndex, OpenXRSourceNames::RightGrip, OutOrientation, OutPosition, WorldToMetersScale);
		}

		XrSession Session = OpenXRHMD->GetSession();

		if (Session == XR_NULL_HANDLE)
		{
			return false;
		}

		XrActionStateGetInfo GetInfo;
		GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
		GetInfo.next = nullptr;
		GetInfo.subactionPath = XR_NULL_PATH;
		GetInfo.action = GetActionForMotionSource(MotionSource);

		XrActionStatePose State;
		State.type = XR_TYPE_ACTION_STATE_POSE;
		State.next = nullptr;
		XrResult Result = xrGetActionStatePose(Session, &GetInfo, &State);
		if (Result >= XR_SUCCESS && State.isActive)
		{
			FQuat Orientation;
			OpenXRHMD->GetCurrentPose(GetDeviceIDForMotionSource(MotionSource), Orientation, OutPosition);
			OutOrientation = FRotator(Orientation);
			return true;
		}
	}

	return false;
}

bool FOpenXRInputPlugin::FOpenXRInput::GetControllerOrientationAndPositionForTime(const int32 ControllerIndex, const FName MotionSource, FTimespan Time, bool& OutTimeWasUsed, FRotator& OutOrientation, FVector& OutPosition, bool& OutbProvidedLinearVelocity, FVector& OutLinearVelocity, bool& OutbProvidedAngularVelocity, FVector& OutAngularVelocityRadPerSec, bool& OutbProvidedLinearAcceleration, FVector& OutLinearAcceleration, float WorldToMetersScale) const
{
	if (OpenXRHMD == nullptr)
	{
		return false;
	}

	if (ControllerIndex == 0 && IsOpenXRInputSupportedMotionSource(MotionSource))
	{
		if (MotionSource == OpenXRSourceNames::AnyHand)
		{
			return GetControllerOrientationAndPositionForTime(ControllerIndex, OpenXRSourceNames::LeftGrip, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale)
				|| GetControllerOrientationAndPositionForTime(ControllerIndex, OpenXRSourceNames::RightGrip, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
		}

		if (MotionSource == OpenXRSourceNames::Left)
		{
			return GetControllerOrientationAndPositionForTime(ControllerIndex, OpenXRSourceNames::LeftGrip, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
		}

		if (MotionSource == OpenXRSourceNames::Right)
		{
			return GetControllerOrientationAndPositionForTime(ControllerIndex, OpenXRSourceNames::RightGrip, Time, OutTimeWasUsed, OutOrientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
		}
	}

	XrSession Session = OpenXRHMD->GetSession();

	if (Session == XR_NULL_HANDLE)
	{
		return false;
	}

	XrActionStateGetInfo GetInfo;
	GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	GetInfo.next = nullptr;
	GetInfo.subactionPath = XR_NULL_PATH;
	GetInfo.action = GetActionForMotionSource(MotionSource);

	if (GetInfo.action == XR_NULL_HANDLE)
	{
		UE_LOG(LogHMD, Warning, TEXT("GetControllerOrientationAndPositionForTime called with motion source %s which is unknown.  Cannot get pose."), *MotionSource.ToString());

		return false;
	}

	XrActionStatePose State;
	State.type = XR_TYPE_ACTION_STATE_POSE;
	State.next = nullptr;
	XrResult Result = xrGetActionStatePose(Session, &GetInfo, &State);
	if (Result >= XR_SUCCESS && State.isActive)
	{
		FQuat Orientation;
		OpenXRHMD->GetPoseForTime(GetDeviceIDForMotionSource(MotionSource), Time, OutTimeWasUsed, Orientation, OutPosition, OutbProvidedLinearVelocity, OutLinearVelocity, OutbProvidedAngularVelocity, OutAngularVelocityRadPerSec, OutbProvidedLinearAcceleration, OutLinearAcceleration, WorldToMetersScale);
		OutOrientation = FRotator(Orientation);
		return true;
	}

	return false;
}

ETrackingStatus FOpenXRInputPlugin::FOpenXRInput::GetControllerTrackingStatus(const int32 ControllerIndex, const FName MotionSource) const
{
	if (OpenXRHMD == nullptr)
	{
		return ETrackingStatus::NotTracked;
	}

	if (ControllerIndex == 0 && IsOpenXRInputSupportedMotionSource(MotionSource))
	{
		if (MotionSource == OpenXRSourceNames::AnyHand)
		{
			if (GetControllerTrackingStatus(ControllerIndex, OpenXRSourceNames::LeftGrip) == ETrackingStatus::Tracked)
			{
				return ETrackingStatus::Tracked;
			}
			else
			{
				return GetControllerTrackingStatus(ControllerIndex, OpenXRSourceNames::RightGrip);
			}
		}

		if (MotionSource == OpenXRSourceNames::Left)
		{
			return GetControllerTrackingStatus(ControllerIndex, OpenXRSourceNames::LeftGrip);
		}

		if (MotionSource == OpenXRSourceNames::Right)
		{
			return GetControllerTrackingStatus(ControllerIndex, OpenXRSourceNames::RightGrip);
		}

		XrSession Session = OpenXRHMD->GetSession();

		if (Session == XR_NULL_HANDLE)
		{
			return ETrackingStatus::NotTracked;
		}

		XrActionStateGetInfo GetInfo;
		GetInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
		GetInfo.next = nullptr;
		GetInfo.subactionPath = XR_NULL_PATH;
		GetInfo.action = GetActionForMotionSource(MotionSource);

		XrActionStatePose State;
		State.type = XR_TYPE_ACTION_STATE_POSE;
		State.next = nullptr;
		XrResult Result = xrGetActionStatePose(Session, &GetInfo, &State);
		if (XR_SUCCEEDED(Result) && State.isActive)
		{
			FQuat Orientation;
			bool bIsTracked = OpenXRHMD->GetIsTracked(GetDeviceIDForMotionSource(MotionSource));
			return bIsTracked ? ETrackingStatus::Tracked : ETrackingStatus::NotTracked;
		}
	}

	return ETrackingStatus::NotTracked;
}

void FOpenXRInputPlugin::FOpenXRInput::EnumerateSources(TArray<FMotionControllerSource>& SourcesOut) const
{
	check(IsInGameThread());

	SourcesOut.Add(OpenXRSourceNames::AnyHand);
	SourcesOut.Add(OpenXRSourceNames::Left);
	SourcesOut.Add(OpenXRSourceNames::Right);
	SourcesOut.Add(OpenXRSourceNames::LeftGrip);
	SourcesOut.Add(OpenXRSourceNames::RightGrip);
	SourcesOut.Add(OpenXRSourceNames::LeftAim);
	SourcesOut.Add(OpenXRSourceNames::RightAim);
}

// TODO: Refactor API to change the Hand type to EControllerHand
void FOpenXRInputPlugin::FOpenXRInput::SetHapticFeedbackValues(int32 ControllerId, int32 Hand, const FHapticFeedbackValues& Values)
{
	if (!bActionsBound || OpenXRHMD == nullptr)
	{
		return;
	}

	XrSession Session = OpenXRHMD->GetSession();

	if (Session == XR_NULL_HANDLE)
	{
		return;
	}
	
	if (!OpenXRHMD->IsFocused())
	{
		return;
	}

	XrHapticVibration HapticValue;
	HapticValue.type = XR_TYPE_HAPTIC_VIBRATION;
	HapticValue.next = nullptr;
	HapticValue.duration = MaxFeedbackDuration;
	HapticValue.frequency = Values.Frequency;
	HapticValue.amplitude = Values.Amplitude;

	if (ControllerId == 0)
	{
		if ((Hand == (int32)EControllerHand::Left || Hand == (int32)EControllerHand::AnyHand) && Controllers.Contains(EControllerHand::Left))
		{
			XrHapticActionInfo HapticActionInfo;
			HapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
			HapticActionInfo.next = nullptr;
			HapticActionInfo.subactionPath = XR_NULL_PATH;
			HapticActionInfo.action = Controllers[EControllerHand::Left].VibrationAction;
			if (Values.HapticBuffer == nullptr && (Values.Amplitude <= 0.0f || Values.Frequency < XR_FREQUENCY_UNSPECIFIED))
			{
				XR_ENSURE(xrStopHapticFeedback(Session, &HapticActionInfo));
			}
			else
			{				
				FOpenXRExtensionChainStructPtrs ScopedExtensionChainStructs;
				if (Values.HapticBuffer != nullptr)
				{
					OpenXRHMD->GetApplyHapticFeedbackAddChainStructsDelegate().Broadcast(&HapticValue, ScopedExtensionChainStructs, Values.HapticBuffer);
				}
				XR_ENSURE(xrApplyHapticFeedback(Session, &HapticActionInfo, (const XrHapticBaseHeader*)&HapticValue));
			}
		}
		if ((Hand == (int32)EControllerHand::Right || Hand == (int32)EControllerHand::AnyHand) && Controllers.Contains(EControllerHand::Right))
		{
			XrHapticActionInfo HapticActionInfo;
			HapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
			HapticActionInfo.next = nullptr;
			HapticActionInfo.subactionPath = XR_NULL_PATH;
			HapticActionInfo.action = Controllers[EControllerHand::Right].VibrationAction;
			if (Values.HapticBuffer == nullptr && (Values.Amplitude <= 0.0f || Values.Frequency < XR_FREQUENCY_UNSPECIFIED))
			{
				XR_ENSURE(xrStopHapticFeedback(Session, &HapticActionInfo));
			}
			else
			{
				FOpenXRExtensionChainStructPtrs ScopedExtensionChainStructs;
				if (Values.HapticBuffer != nullptr)
				{
					OpenXRHMD->GetApplyHapticFeedbackAddChainStructsDelegate().Broadcast(&HapticValue, ScopedExtensionChainStructs, Values.HapticBuffer);
				}
				XR_ENSURE(xrApplyHapticFeedback(Session, &HapticActionInfo, (const XrHapticBaseHeader*)&HapticValue));
			}
		}
		if (Hand == (int32)EControllerHand::HMD)
		{
			XrHapticActionInfo HapticActionInfo;
			HapticActionInfo.type = XR_TYPE_HAPTIC_ACTION_INFO;
			HapticActionInfo.next = nullptr;
			HapticActionInfo.subactionPath = XR_NULL_PATH;
			HapticActionInfo.action = Controllers[EControllerHand::HMD].VibrationAction;
			
			if (Values.HapticBuffer == nullptr && (Values.Amplitude <= 0.0f || Values.Frequency < XR_FREQUENCY_UNSPECIFIED))
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

#undef LOCTEXT_NAMESPACE // "OpenXRInputPlugin"
