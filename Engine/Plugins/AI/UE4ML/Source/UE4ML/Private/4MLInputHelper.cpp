// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLInputHelper.h"
#include "GameFramework/InputSettings.h"


namespace F4MLInputHelper
{
	void CreateInputMap(TArray<TTuple<FKey, FName>>& InterfaceKeys, TMap<FKey, int32>& FKeyToInterfaceKeyMap)
	{
		UInputSettings* InputSettings = UInputSettings::GetInputSettings();
		if (ensure(InputSettings))
		{
			TMap<FName, TArray<FKey>> AvailableActions;

			for (const FInputActionKeyMapping& ActionKey : InputSettings->GetActionMappings())
			{
				TArray<FKey>& Keys = AvailableActions.FindOrAdd(ActionKey.ActionName);
				Keys.Add(ActionKey.Key);
			}

			for (const FInputAxisKeyMapping& AxisAction : InputSettings->GetAxisMappings())
			{
				const bool bIsKeyboard = AxisAction.Key.IsGamepadKey() == false && AxisAction.Key.IsMouseButton() == false;
				const FString ActionName = FString::Printf(TEXT("%s%s")
					, AxisAction.Scale > 0 ? TEXT("+") : TEXT("-")
					, *AxisAction.AxisName.ToString());
				const FName ActionFName(*ActionName);

				if (bIsKeyboard && AvailableActions.Find(ActionFName) == nullptr)
				{
					AvailableActions.Add(ActionFName, { AxisAction.Key });
				}
			}

			for (auto KeyValue : AvailableActions)
			{
				const int32 Index = InterfaceKeys.Add(TTuple<FKey, FName>(KeyValue.Value[0], KeyValue.Key));
				for (auto Key : KeyValue.Value)
				{
					ensure(FKeyToInterfaceKeyMap.Find(Key) == nullptr);
					FKeyToInterfaceKeyMap.Add(Key, Index);
				}
			}
		}
	}
}