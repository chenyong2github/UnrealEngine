// Copyright Epic Games, Inc. All Rights Reserved.

#include "SimpleController.h"
#include "InputCoreTypes.h"
#include "OpenXRCore.h"

#include <openxr/openxr.h>

#define LOCTEXT_NAMESPACE "SimpleController"

IMPLEMENT_MODULE(FSimpleController, SimpleController)

namespace SimpleKeys
{
	const FKey SimpleController_Left_Select_Click("SimpleController_Left_Select_Click");
	const FKey SimpleController_Left_Menu_Click("SimpleController_Left_Menu_Click");

	const FKey SimpleController_Right_Select_Click("SimpleController_Right_Select_Click");
	const FKey SimpleController_Right_Menu_Click("SimpleController_Right_Menu_Click");
}

void FSimpleController::StartupModule()
{
	RegisterOpenXRExtensionModularFeature();

	EKeys::AddMenuCategoryDisplayInfo("SimpleController", LOCTEXT("SimpleControllerSubCategory", "Simple Controller"), TEXT("GraphEditor.PadEvent_16x"));

	EKeys::AddKey(FKeyDetails(SimpleKeys::SimpleController_Left_Select_Click, LOCTEXT("SimpleController_Left_Select_Click", "Simple Controller (L) Select"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "SimpleController"));
	EKeys::AddKey(FKeyDetails(SimpleKeys::SimpleController_Left_Menu_Click, LOCTEXT("SimpleController_Left_Menu_Click", "Simple Controller (L) Menu"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "SimpleController"));
	EKeys::AddKey(FKeyDetails(SimpleKeys::SimpleController_Right_Select_Click, LOCTEXT("SimpleController_Right_Select_Click", "Simple Controller (R) Select"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "SimpleController"));
	EKeys::AddKey(FKeyDetails(SimpleKeys::SimpleController_Right_Menu_Click, LOCTEXT("SimpleController_Right_Menu_Click", "Simple Controller (R) Menu"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "SimpleController"));
}

bool FSimpleController::GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics)
{
	OutKeyPrefix = "SimpleController";
	OutHasHaptics = true;
    return xrStringToPath(InInstance, "/interaction_profiles/khr/simple_controller", &OutPath) == XR_SUCCESS;
}

#undef LOCTEXT_NAMESPACE
