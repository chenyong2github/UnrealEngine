// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VPUtilitiesEditorSettings.h"

void UVPUtilitiesEditorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	FProperty* MemberPropertyThatChanged = PropertyChangedEvent.MemberProperty;
	const FName MemberPropertyName = MemberPropertyThatChanged != NULL ? MemberPropertyThatChanged->GetFName() : NAME_None;
	if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVPUtilitiesEditorSettings, bUseTransformGizmo))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.ShowTransformGizmo"));
		CVar->Set(bUseTransformGizmo);
	}
	else if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UVPUtilitiesEditorSettings, bUseGripInertiaDamping))
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("VI.HighSpeedInertiaDamping"));
		if (bUseGripInertiaDamping)
		{
			CVar->Set(InertiaDamping);
		}
		else
		{
			CVar->Set(0);
		}
	}
}