// Copyright Epic Games, Inc. All Rights Reserved.

#include "OpenXRMsftHandInteraction.h"
#include "Modules/ModuleManager.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"

#define LOCTEXT_NAMESPACE "OpenXRMsftHandInteraction"

IMPLEMENT_MODULE(FOpenXRMsftHandInteraction, OpenXRMsftHandInteraction);

namespace OpenXRMsftHandInteractionKeys
{
	const FKey OpenXRMsftHandInteraction_Left_Select_Click("OpenXRMsftHandInteraction_Left_Select_Value");
	const FKey OpenXRMsftHandInteraction_Right_Select_Click("OpenXRMsftHandInteraction_Right_Select_Value");

	const FKey OpenXRMsftHandInteraction_Right_Grip_Click("OpenXRMsftHandInteraction_Right_Grip_Value");
	const FKey OpenXRMsftHandInteraction_Left_Grip_Click("OpenXRMsftHandInteraction_Left_Grip_Value");

	const FKey OpenXRMsftHandInteraction_Right_Grip_Axis("OpenXRMsftHandInteraction_Right_Grip_Axis");
	const FKey OpenXRMsftHandInteraction_Left_Grip_Axis("OpenXRMsftHandInteraction_Left_Grip_Axis");
}

void FOpenXRMsftHandInteraction::StartupModule()
{
	RegisterOpenXRExtensionModularFeature();

	EKeys::AddMenuCategoryDisplayInfo("OpenXRMsftHandInteraction", LOCTEXT("OpenXRMsftHandInteractionSubCategory", "OpenXR Msft Hand Interaction"), TEXT("GraphEditor.PadEvent_16x"));

	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Left_Select_Click,	LOCTEXT("OpenXRMsftHandInteraction_Left_Select_Click", "OpenXRMsftHandInteraction (L) Select"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));
	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Right_Select_Click, LOCTEXT("OpenXRMsftHandInteraction_Right_Select_Click", "OpenXRMsftHandInteraction (R) Select"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));

	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Left_Grip_Click, LOCTEXT("OpenXRMsftHandInteraction_Left_Grip_Click", "OpenXRMsftHandInteraction (L) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));
	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Right_Grip_Click, LOCTEXT("OpenXRMsftHandInteraction_Right_Grip_Click", "OpenXRMsftHandInteraction (R) Grip"), FKeyDetails::GamepadKey | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));

	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Right_Grip_Axis, LOCTEXT("OpenXRMsftHandInteraction_Right_Grip_Axis", "OpenXRMsftHandInteraction (R) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));
	EKeys::AddKey(FKeyDetails(OpenXRMsftHandInteractionKeys::OpenXRMsftHandInteraction_Left_Grip_Axis,	LOCTEXT("OpenXRMsftHandInteraction_Left_Grip_Axis", "OpenXRMsftHandInteraction (L) Grip Axis"), FKeyDetails::GamepadKey | FKeyDetails::Axis1D | FKeyDetails::NotBlueprintBindableKey, "OpenXRMsftHandInteraction"));
}

bool FOpenXRMsftHandInteraction::GetRequiredExtensions(TArray<const ANSICHAR*>& OutExtensions)
{
	OutExtensions.Add("XR_MSFT_hand_interaction");
	return true;
}

bool FOpenXRMsftHandInteraction::GetInteractionProfile(XrInstance InInstance, FString& OutKeyPrefix, XrPath& OutPath, bool& OutHasHaptics)
{
	OutKeyPrefix = "OpenXRMsftHandInteraction";
	OutHasHaptics = false;
	return xrStringToPath(InInstance, "/interaction_profiles/microsoft/hand_interaction", &OutPath) == XR_SUCCESS;
}
