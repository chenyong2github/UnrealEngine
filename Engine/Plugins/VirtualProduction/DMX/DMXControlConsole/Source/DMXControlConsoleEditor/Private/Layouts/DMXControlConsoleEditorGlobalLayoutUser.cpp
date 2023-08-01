// Copyright Epic Games, Inc. All Rights Reserved.


#include "DMXControlConsoleEditorGlobalLayoutUser.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderGroup.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorGlobalLayoutUser"

void UDMXControlConsoleEditorGlobalLayoutUser::SetIsActive(bool bActive)
{
	const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> AllFaderGroups = GetAllFaderGroups();
	for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : AllFaderGroups)
	{
		if (FaderGroup.IsValid())
		{
			FaderGroup->SetIsActive(bActive);
		}
	}
}

#undef LOCTEXT_NAMESPACE
