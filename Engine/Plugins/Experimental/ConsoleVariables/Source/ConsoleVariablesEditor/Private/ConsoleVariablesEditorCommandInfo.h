// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/Find.h"
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

	struct FStaticConsoleVariableFlagInfo
	{
		EConsoleVariableFlags Flag;
		FText DisplayText;
	};

	FConsoleVariablesEditorCommandInfo()
	{
		ConsoleVariablePtr = nullptr;
	}

	FConsoleVariablesEditorCommandInfo(
		const FString& InCommand, IConsoleVariable* InVariablePtr, const FDelegateHandle& InOnVariableChangedCallbackHandle)
	: Command(InCommand)
	, ConsoleVariablePtr(InVariablePtr)
	, OnVariableChangedCallbackHandle(InOnVariableChangedCallbackHandle)
	{
		StartupValueAsString = ConsoleVariablePtr->GetString();
		StartupSource = GetSource();
	}

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

	EConsoleVariableFlags GetSource() const
	{
		if (ConsoleVariablePtr)
		{
			return (EConsoleVariableFlags)((uint32)ConsoleVariablePtr->GetFlags() & ECVF_SetByMask);
		}

		return ECVF_Default;
	}

	void ClearSourceFlags() const
	{
		for (const FStaticConsoleVariableFlagInfo StaticConsoleVariableFlagInfo : SupportedFlags)
		{
			ConsoleVariablePtr->ClearFlags(StaticConsoleVariableFlagInfo.Flag);
		}
	}

	void SetSourceFlag(const EConsoleVariableFlags InSource) const
	{
		ClearSourceFlags();
		ConsoleVariablePtr->SetFlags((EConsoleVariableFlags)InSource);
	}

	FText GetSourceAsText() const
	{
		return ConvertConsoleVariableSetByFlagToText(GetSource());
	}

	static FText ConvertConsoleVariableSetByFlagToText(const EConsoleVariableFlags InFlag)
	{
		FText ReturnValue = LOCTEXT("UnknownSource", "<UNKNOWN>"); 

		if (const FStaticConsoleVariableFlagInfo* Match = Algo::FindByPredicate(SupportedFlags,
				[InFlag](const FStaticConsoleVariableFlagInfo& Comparator)
				{
					return Comparator.Flag == InFlag;
				}))
		{
			ReturnValue = (*Match).DisplayText;
		}
		
		return ReturnValue;
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

	/** The value of this command when the module started in this session after it may have been set by an ini file. */
	FString StartupValueAsString;
	/** The source of this variable last setting as recorded when the plugin was loaded. */
	EConsoleVariableFlags StartupSource;
	
	FDelegateHandle OnVariableChangedCallbackHandle;

	static const inline TArray<FStaticConsoleVariableFlagInfo> SupportedFlags =
	{
		{ EConsoleVariableFlags::ECVF_SetByConstructor, LOCTEXT("SetByConstructor", "Constructor") },
		{ EConsoleVariableFlags::ECVF_SetByScalability, LOCTEXT("SetByScalability", "Scalability") },
		{ EConsoleVariableFlags::ECVF_SetByGameSetting, LOCTEXT("SetByGameSetting", "Game Setting") },
		{ EConsoleVariableFlags::ECVF_SetByProjectSetting, LOCTEXT("SetByProjectSetting", "Project Setting") },
		{ EConsoleVariableFlags::ECVF_SetBySystemSettingsIni, LOCTEXT("SetBySystemSettingsIni", "System Settings ini") },
		{ EConsoleVariableFlags::ECVF_SetByDeviceProfile, LOCTEXT("SetByDeviceProfile", "Device Profile") },
		{ EConsoleVariableFlags::ECVF_SetByGameOverride, LOCTEXT("SetByGameOverride", "Game Override") },
		{ EConsoleVariableFlags::ECVF_SetByConsoleVariablesIni, LOCTEXT("SetByConsoleVariablesIni", "Console Variables ini") },
		{ EConsoleVariableFlags::ECVF_SetByCommandline, LOCTEXT("SetByCommandline", "Command line") },
		{ EConsoleVariableFlags::ECVF_SetByCode, LOCTEXT("SetByCode", "Code") },
		{ EConsoleVariableFlags::ECVF_SetByConsole, LOCTEXT("SetByConsole", "Console") }
	};
};

#undef LOCTEXT_NAMESPACE
