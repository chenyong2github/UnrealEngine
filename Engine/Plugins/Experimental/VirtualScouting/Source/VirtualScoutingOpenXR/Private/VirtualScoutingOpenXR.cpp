// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualScoutingOpenXR.h"

#if WITH_EDITOR

#include "Logging/LogMacros.h"
#include "IVREditorModule.h"
#include "ViewportWorldInteraction.h"
#include "VREditorMode.h"
#include "VREditorInteractor.h"
#include "VRModeSettings.h"


#define LOCTEXT_NAMESPACE "VirtualScouting"


DECLARE_LOG_CATEGORY_EXTERN(LogVPOpenXRDebug, VeryVerbose, All);
DEFINE_LOG_CATEGORY(LogVPOpenXRDebug);


static TAutoConsoleVariable<int32> CVarOpenXRDebugLogging(
	TEXT("VirtualScouting.OpenXRDebugLogging"),
	0,
	TEXT("If true, register an Unreal log sink via XR_EXT_debug_utils.\n"),
	ECVF_Default);


class FVirtualScoutingOpenXRModule : public IModuleInterface
{
private:
	virtual void StartupModule() override
	{
		OpenXRExt = MakeUnique<FVirtualScoutingOpenXRExtension>();
	}

	virtual void ShutdownModule() override
	{
		OpenXRExt.Reset();
	}

private:
	TUniquePtr<FVirtualScoutingOpenXRExtension> OpenXRExt;
};


IMPLEMENT_MODULE(FVirtualScoutingOpenXRModule, VirtualScoutingOpenXR);


FVirtualScoutingOpenXRExtension::FVirtualScoutingOpenXRExtension()
{
	RegisterOpenXRExtensionModularFeature();

	InitCompleteDelegate = FCoreDelegates::OnFEngineLoopInitComplete.AddLambda(
		[this]()
		{
			IVREditorModule::Get().OnVREditingModeEnter().AddRaw(this, &FVirtualScoutingOpenXRExtension::OnVREditingModeEnter);
			IVREditorModule::Get().OnVREditingModeExit().AddRaw(this, &FVirtualScoutingOpenXRExtension::OnVREditingModeExit);

			// Must happen last; this implicitly deallocates this lambda's captures.
			FCoreDelegates::OnFEngineLoopInitComplete.Remove(InitCompleteDelegate);
		}
	);
}


FVirtualScoutingOpenXRExtension::~FVirtualScoutingOpenXRExtension()
{
#if 0 // FIXME?: Too late to use Instance here, and don't see any suitable extension interface methods.
	// Might also be OK not to explicitly clean this up.
	if (Messenger != XR_NULL_HANDLE)
	{
		PFN_xrDestroyDebugUtilsMessengerEXT PfnXrDestroyDebugUtilsMessengerEXT;
		if (XR_ENSURE(xrGetInstanceProcAddr(Instance, "xrDestroyDebugUtilsMessengerEXT",
			reinterpret_cast<PFN_xrVoidFunction*>(&PfnXrDestroyDebugUtilsMessengerEXT))))
		{
			XR_ENSURE(PfnXrDestroyDebugUtilsMessengerEXT(Messenger));
			Messenger = XR_NULL_HANDLE;
		}
	}
#endif

	UnregisterOpenXRExtensionModularFeature();

	if (IVREditorModule::IsAvailable())
	{
		IVREditorModule::Get().OnVREditingModeEnter().RemoveAll(this);
		IVREditorModule::Get().OnVREditingModeExit().RemoveAll(this);
	}
}


bool FVirtualScoutingOpenXRExtension::GetOptionalExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	if (CVarOpenXRDebugLogging.GetValueOnAnyThread() != 0)
	{
		OutExtensions.Add(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return true;
}


void FVirtualScoutingOpenXRExtension::PostCreateInstance(XrInstance InInstance)
{
	Instance = InInstance;

	if (CVarOpenXRDebugLogging.GetValueOnAnyThread() != 0)
	{
		PFN_xrCreateDebugUtilsMessengerEXT PfnXrCreateDebugUtilsMessengerEXT;
		if (XR_ENSURE(xrGetInstanceProcAddr(Instance, "xrCreateDebugUtilsMessengerEXT",
			reinterpret_cast<PFN_xrVoidFunction*>(&PfnXrCreateDebugUtilsMessengerEXT))))
		{
			XrDebugUtilsMessengerCreateInfoEXT DebugMessengerCreateInfo;
			DebugMessengerCreateInfo.type = XR_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
			DebugMessengerCreateInfo.next = nullptr;
			DebugMessengerCreateInfo.messageSeverities =
				XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
			DebugMessengerCreateInfo.messageTypes =
				XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
				XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT;
			DebugMessengerCreateInfo.userCallback = &FVirtualScoutingOpenXRExtension::XrDebugUtilsMessengerCallback_Trampoline;
			DebugMessengerCreateInfo.userData = this;

			if (XR_ENSURE(PfnXrCreateDebugUtilsMessengerEXT(Instance, &DebugMessengerCreateInfo, &Messenger)))
			{
				UE_LOG(LogVPOpenXRDebug, Log, TEXT("XR_EXT_debug_utils messenger ACTIVE"));
				return;
			}
		}
	}

	UE_LOG(LogVPOpenXRDebug, Log, TEXT("XR_EXT_debug_utils messenger DISABLED"));
}


void FVirtualScoutingOpenXRExtension::AddActions(XrInstance InInstance, FCreateActionSetFunc CreateActionSetFunc, FCreateActionFunc CreateActionFunc)
{
	// Clean up any previously created action set, which also implicitly cleans up the actions belonging to it.
	if (ActionSet != XR_NULL_HANDLE)
	{
		XR_ENSURE(xrDestroyActionSet(ActionSet));
		ActionSet = XR_NULL_HANDLE;
	}

	UClass* InteractorClass = GetDefault<UVRModeSettings>()->InteractorClass.LoadSynchronous();
	if (!InteractorClass)
	{
		return;
	}

	const UVREditorInteractor* InteractorCDO = Cast<const UVREditorInteractor>(InteractorClass->GetDefaultObject());
	if (!InteractorCDO)
	{
		return;
	}

	// Create action set
	{
		FActionSetParams ActionSetParams;
		ActionSetParams.Name = "ue_vp_scouting";
		ActionSetParams.LocalizedName = LOCTEXT("ActionSetName", "Virtual Scouting");
		ActionSetParams.Priority = 100;
		ActionSet = CreateActionSetFunc(ActionSetParams);
	}

	XrPath LeftHand, RightHand;
	check(XR_SUCCEEDED(xrStringToPath(Instance, "/user/hand/left", &LeftHand)));
	check(XR_SUCCEEDED(xrStringToPath(Instance, "/user/hand/right", &RightHand)));

	FActionParams ActionParams;
	ActionParams.Set = ActionSet;
	ActionParams.SubactionPaths = { LeftHand, RightHand };

	for (TPair<FViewportActionKeyInput, TArray<FKey>>& ActionMappings : InteractorCDO->GetKnownActionMappings())
	{
		FViewportActionKeyInput& Action = ActionMappings.Key;
		TArray<FKey>& Keys = ActionMappings.Value;

		ActionParams.Type = Action.bIsAxis ? XR_ACTION_TYPE_FLOAT_INPUT : XR_ACTION_TYPE_BOOLEAN_INPUT;
		ActionParams.Name = Action.ActionType;
		// TODO: ActionParams.LocalizedName
		ActionParams.SuggestedBindings = MoveTemp(Keys);
		CreateActionFunc(ActionParams);
	}
}


void FVirtualScoutingOpenXRExtension::GetActiveActionSetsForSync(TArray<XrActiveActionSet>& OutActiveSets)
{
	if (bIsVrEditingModeActive && ensure(ActionSet != XR_NULL_HANDLE))
	{
		XrActiveActionSet ActiveSet;
		ActiveSet.actionSet = ActionSet;
		ActiveSet.subactionPath = XR_NULL_PATH;
		OutActiveSets.Add(ActiveSet);
	}
}


void FVirtualScoutingOpenXRExtension::OnVREditingModeEnter()
{
	bIsVrEditingModeActive = true;
}


void FVirtualScoutingOpenXRExtension::OnVREditingModeExit()
{
	bIsVrEditingModeActive = false;
}


//static
XrBool32 XRAPI_CALL FVirtualScoutingOpenXRExtension::XrDebugUtilsMessengerCallback_Trampoline(
	XrDebugUtilsMessageSeverityFlagsEXT InMessageSeverity,
	XrDebugUtilsMessageTypeFlagsEXT InMessageTypes,
	const XrDebugUtilsMessengerCallbackDataEXT* InCallbackData,
	void* InUserData)
{
	// TODO?: InUserData contains the FVirtualScoutingOpenXRExtension*, could forward to an instance method.
	// However, doing the work directly in here seems fine for now.

	ELogVerbosity::Type Verbosity;
	switch (InMessageSeverity)
	{
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: Verbosity = ELogVerbosity::Verbose; break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    Verbosity = ELogVerbosity::Display; break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: Verbosity = ELogVerbosity::Warning; break;
		case XR_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   Verbosity = ELogVerbosity::Error;   break;
		default:
			ensureMsgf(false, TEXT("Unhandled XrDebugUtilsMessageSeverityFlagsEXT: %X"), InMessageSeverity);
			Verbosity = ELogVerbosity::Error;
			break;
	}

	// Results in "____" if no bits are set, or "GVPC" if all bits are set, or some combination.
	FString Types = TEXT("####");
	Types[0] = (InMessageTypes & XR_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     ? TEXT('G') : TEXT('_');
	Types[1] = (InMessageTypes & XR_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  ? TEXT('V') : TEXT('_');
	Types[2] = (InMessageTypes & XR_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) ? TEXT('P') : TEXT('_');
	Types[3] = (InMessageTypes & XR_DEBUG_UTILS_MESSAGE_TYPE_CONFORMANCE_BIT_EXT) ? TEXT('C') : TEXT('_');

	FMsg::Logf(__FILE__, __LINE__, LogVPOpenXRDebug.GetCategoryName(), Verbosity,
		TEXT("[%s]: %S(): %S"), *Types, InCallbackData->functionName, InCallbackData->message);

	// "A value of XR_TRUE indicates that the application wants to abort this call. [...]
	// Applications should always return XR_FALSE so that they see the same behavior with
	// and without validation layers enabled."
	return XR_FALSE;
}


#undef LOCTEXT_NAMESPACE

#else // not WITH_EDITOR

IMPLEMENT_MODULE(FDefaultModuleImpl, VirtualScoutingOpenXR);

#endif // #if WITH_EDITOR .. #else
