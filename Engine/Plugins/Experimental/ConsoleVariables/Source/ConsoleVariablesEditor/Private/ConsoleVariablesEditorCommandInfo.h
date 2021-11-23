// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConsoleVariablesEditorLog.h"
#include "Editor.h"
#include "Engine/GameEngine.h"

#include "ConsoleVariablesEditorCommandInfo.generated.h"

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

USTRUCT()
struct FConsoleVariablesEditorCommandInfo
{
	GENERATED_BODY()

	FConsoleVariablesEditorCommandInfo()
	{
		ConsoleVariablePtr = nullptr;
	}

	FConsoleVariablesEditorCommandInfo(
		const FString& InCommand, IConsoleVariable* InVariablePtr, const FString& InStartupValue, const FDelegateHandle& InOnVariableChangedCallbackHandle)
	: Command(InCommand)
	, ConsoleVariablePtr(InVariablePtr)
	, StartupValueAsString(InStartupValue)
	, OnVariableChangedCallbackHandle(InOnVariableChangedCallbackHandle)
	{}

	FORCEINLINE bool operator==(const FConsoleVariablesEditorCommandInfo& Comparator) const
	{
		return ConsoleVariablePtr && Comparator.ConsoleVariablePtr &&
			ConsoleVariablePtr == Comparator.ConsoleVariablePtr &&
			Command.Equals(Comparator.Command);
	}

	void ExecuteCommand(const FString& NewValueAsString) const
	{
		GEngine->Exec(GetCurrentWorld(), *FString::Printf(TEXT("%s %s"), *Command, *NewValueAsString));
	}
	
	static UWorld* GetCurrentWorld()
	{
		UWorld* CurrentWorld = nullptr;
		if (GIsEditor)
		{
			CurrentWorld = GEditor->GetEditorWorldContext().World();
		}
		else if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
		{
			CurrentWorld = GameEngine->GetGameWorld();
		}
		return CurrentWorld;
	}

	FText GetSource() const
	{
		if (ConsoleVariablePtr)
		{
			const EConsoleVariableFlags SetBy = (EConsoleVariableFlags)((uint32)ConsoleVariablePtr->GetFlags() & ECVF_SetByMask);

			switch (SetBy)
			{
			case (EConsoleVariableFlags::ECVF_SetByConstructor):
				return LOCTEXT("SetByConstructor", "Constructor");
				
			case (EConsoleVariableFlags::ECVF_SetByScalability):
				return LOCTEXT("SetByScalability", "Scalability");
				
			case (EConsoleVariableFlags::ECVF_SetByGameSetting):
				return LOCTEXT("SetByGameSetting", "Game Setting");
				
			case (EConsoleVariableFlags::ECVF_SetByProjectSetting):
				return LOCTEXT("SetByProjectSetting", "Project Setting");
				
			case (EConsoleVariableFlags::ECVF_SetBySystemSettingsIni):
				return LOCTEXT("SetBySystemSettingsIni", "System Settings ini");
				
			case (EConsoleVariableFlags::ECVF_SetByDeviceProfile):
				return LOCTEXT("SetByDeviceProfile", "Device Profile");
				
			case (EConsoleVariableFlags::ECVF_SetByGameOverride):
				return LOCTEXT("SetByGameOverride", "Game Override");
				
			case (EConsoleVariableFlags::ECVF_SetByConsoleVariablesIni):
				return LOCTEXT("SetByConsoleVariablesIni", "Console Variables ini");
				
			case (EConsoleVariableFlags::ECVF_SetByCommandline):
				return LOCTEXT("SetByCommandline", "Command line");
				
			case (EConsoleVariableFlags::ECVF_SetByCode):
				return LOCTEXT("SetByCode", "Code");
				
			case (EConsoleVariableFlags::ECVF_SetByConsole):
				return LOCTEXT("SetByConsole", "Console");

			default:
				break;;
			}
		}
		
		return LOCTEXT("UnknownSource", "<UNKNOWN>");
	}

	bool IsCurrentValueDifferentFromInputValue(const FString& InValueToCompare) const
	{
		if (ConsoleVariablePtr)
		{
			return !ConsoleVariablePtr->GetString().Equals(InValueToCompare);
		}

		return false;
	}

	/** The actual string command to execute */
	UPROPERTY()
	FString Command;

	IConsoleVariable* ConsoleVariablePtr;

	/** The value of this command when the module started in this session after it may have been set by an ini file */
	FString StartupValueAsString;
	
	FDelegateHandle OnVariableChangedCallbackHandle;
};

#undef LOCTEXT_NAMESPACE
