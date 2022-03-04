// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/Find.h"
#include "CoreMinimal.h"
#include "ConsoleVariablesEditorLog.h"
#include "Editor.h"
#include "Engine/GameEngine.h"
#include "HAL/IConsoleManager.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnDetectConsoleObjectUnregistered, FString)

#define LOCTEXT_NAMESPACE "ConsoleVariablesEditor"

struct FConsoleVariablesEditorCommandInfo
{
	enum class EConsoleObjectType
	{
		// A console command that has no associated console object but is parsed externally, e.g. 'stat unit'
		NullObject,
		// A console command with an associated console object, like 'r.SetNearClipPlane'
		Command,
		// A console variable such as 'r.ScreenPercentage'
		Variable
	};

	struct FStaticConsoleVariableFlagInfo
	{
		EConsoleVariableFlags Flag;
		FText DisplayText;
	};

	FConsoleVariablesEditorCommandInfo(const FString& InCommand)
	: Command(InCommand)
	{
		if (GetConsoleObjectPtr())
		{
			ObjectType = EConsoleObjectType::Command;
			
			if (const IConsoleVariable* AsVariable = GetConsoleVariablePtr())
			{
				ObjectType = EConsoleObjectType::Variable;
				StartupValueAsString = AsVariable->GetString();
				StartupSource = GetSource();
			}
		}
	}

	~FConsoleVariablesEditorCommandInfo()
	{
		OnDetectConsoleObjectUnregistered.Remove(OnDetectConsoleObjectUnregisteredHandle);

		if (IConsoleVariable* AsVariable = GetConsoleVariablePtr())
		{
			AsVariable->OnChangedDelegate().Remove(OnVariableChangedCallbackHandle);
		}
	}

	FORCEINLINE bool operator==(const FConsoleVariablesEditorCommandInfo& Comparator) const
	{
		return Command.Equals(Comparator.Command);
	}

	void SetIfChangedInCurrentPreset(const bool bNewSetting)
	{
		bSetInCurrentSession = bNewSetting;
	}

	/** Sets a variable to the specified value whilst maintaining its SetBy flag.
	  * Non-variables will be executed through the console.
	  * If bSetInSession is true, this CommandInfo's associated variable row will display "Session" in the UI.
	 */
	void ExecuteCommand(const FString& NewValueAsString, const bool bSetInSession = true)
	{
		if (IConsoleVariable* AsVariable = GetConsoleVariablePtr())
		{
			AsVariable->Set(*NewValueAsString, GetSource());

			bSetInCurrentSession = bSetInSession;
		}
		else
		{
			GEngine->Exec(GetCurrentWorld(),
				*FString::Printf(TEXT("%s %s"), *Command, *NewValueAsString).TrimStartAndEnd());
		}
	}

	/** Get a reference to the cached console object. May return nullptr if unregistered. */
	IConsoleObject* GetConsoleObjectPtr()
	{
		// If the console object ptr goes stale or is older than the specified threshold, try to refresh it
		// May return nullptr if unregistered
		if (!ConsoleObjectPtr ||
			(FDateTime::UtcNow() - TimeOfLastConsoleObjectRefresh).GetTotalSeconds() > ConsoleObjectRefreshThreshold)
		{
			FString CommandKey = Command; 

			// Remove additional params, if they exist
			const int32 IndexOfSpace = CommandKey.Find(" ");
			if (IndexOfSpace != INDEX_NONE)
			{
				CommandKey = CommandKey.Left(IndexOfSpace).TrimStartAndEnd();
			}
			
			ConsoleObjectPtr = IConsoleManager::Get().FindConsoleObject(*CommandKey);
			TimeOfLastConsoleObjectRefresh = FDateTime::UtcNow();
		}

		// If the console object turns out to be unregistered, let interested parties know
		if (ConsoleObjectPtr && ConsoleObjectPtr->TestFlags(ECVF_Unregistered))
		{
			OnDetectConsoleObjectUnregistered.Broadcast(Command);
		}

		return ConsoleObjectPtr;
	}

	/** Return the console object as a console variable object if applicable. May return nullptr if unregistered. */
	IConsoleVariable* GetConsoleVariablePtr()
	{
		if (IConsoleObject* ObjectPtr = GetConsoleObjectPtr())
		{
			return ObjectPtr->AsVariable();
		}
		
		return nullptr;
	}

	/**
	 *Return the console object as a console command object if applicable.
	 *Does not consider externally parsed console commands, as they have no associated objects.
	 */
	IConsoleCommand* GetConsoleCommandPtr()
	{
		if (IConsoleObject* ObjectPtr = GetConsoleObjectPtr())
		{
			return ObjectPtr->AsCommand();
		}
		
		return nullptr;
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

	FString GetHelpText()
	{
		if (const IConsoleVariable* AsVariable = GetConsoleVariablePtr())
		{
			return FString(AsVariable->GetHelp());
		}

		return "";
	}

	EConsoleVariableFlags GetSource()
	{
		if (const IConsoleObject* ConsoleObject = GetConsoleObjectPtr())
		{
			return (EConsoleVariableFlags)((uint32)ConsoleObject->GetFlags() & ECVF_SetByMask);
		}

		return ECVF_Default;
	}

	void ClearSourceFlags()
	{
		if (IConsoleObject* ConsoleObject = GetConsoleObjectPtr())
		{
			for (const FStaticConsoleVariableFlagInfo& StaticConsoleVariableFlagInfo : SupportedFlags)
			{
				ConsoleObject->ClearFlags(StaticConsoleVariableFlagInfo.Flag);
			}
		}
	}

	void SetSourceFlag(const EConsoleVariableFlags InSource)
	{
		if (IConsoleVariable* AsVariable = GetConsoleVariablePtr())
		{
			AsVariable->Set(*AsVariable->GetString(), StartupSource);
			return;
		}
		
		const uint32 OldPri = (uint32)GetSource();
		const uint32 NewPri = (uint32)InSource;

		if (NewPri < OldPri)
		{
			return;
		}
		
		if (IConsoleObject* ConsoleObject = GetConsoleObjectPtr())
		{
			ClearSourceFlags();
			ConsoleObject->SetFlags((EConsoleVariableFlags)InSource);
		}
	}

	FText GetSourceAsText()
	{
		// Non-variables don't really have a source
		if (ObjectType != EConsoleObjectType::Variable)
		{
			return LOCTEXT("Source_IsNotConsoleVariableButConsoleCommand", "Command");
		}

		if (bSetInCurrentSession)
		{
			return LOCTEXT("Source_SetByCurrentPreset", "Session");
		}
		
		return ConvertConsoleVariableSetByFlagToText(GetSource());
	}

	static FText ConvertConsoleVariableSetByFlagToText(const EConsoleVariableFlags InFlag)
	{
		FText ReturnValue = LOCTEXT("UnknownSource", "<UNKNOWN>"); 

		if (const FStaticConsoleVariableFlagInfo* Match = Algo::FindByPredicate(
			SupportedFlags,
				[InFlag](const FStaticConsoleVariableFlagInfo& Comparator)
				{
					return Comparator.Flag == InFlag;
				}))
		{
			ReturnValue = (*Match).DisplayText;
		}
		
		return ReturnValue;
	}

	bool IsCurrentValueDifferentFromInputValue(const FString& InValueToCompare)
	{
		if (const IConsoleVariable* AsVariable = GetConsoleVariablePtr())
		{
			// Floats sometimes return true erroneously because they can be stringified as e.g '1' or '1.0' by different functions.
			if (AsVariable->IsVariableFloat())
			{
				const float A = AsVariable->GetFloat();
				const float B = FCString::Atof(*InValueToCompare);

				return !FMath::IsNearlyEqual(A, B);
			}
			
			return !AsVariable->GetString().Equals(InValueToCompare);
		}
		else if (ObjectType == EConsoleObjectType::NullObject || ObjectType == EConsoleObjectType::Command)
		{
			return true;
		}

		return false;
	}

	/** The actual string key or name */
	UPROPERTY()
	FString Command;

	EConsoleObjectType ObjectType = EConsoleObjectType::NullObject;

	/** This object is periodically refreshed to mitigate the occurrence of stale pointers. */
	IConsoleObject* ConsoleObjectPtr;
	FDateTime TimeOfLastConsoleObjectRefresh;

	double ConsoleObjectRefreshThreshold = 1.0;
	
	/** The value of this variable (if Variable object type) when the module started in this session after it may have been set by an ini file. */
	FString StartupValueAsString;
	
	/** The source of this variable's (if Variable object type) last setting as recorded when the plugin was loaded. */
	EConsoleVariableFlags StartupSource = ECVF_Default;

	/** If the variable was last changed by the current preset */
	bool bSetInCurrentSession = false;

	/** When variables change, this callback is executed. */
	FDelegateHandle OnVariableChangedCallbackHandle;

	/** When commands are unregistered change, this callback is broadcasted. */
	FOnDetectConsoleObjectUnregistered OnDetectConsoleObjectUnregistered;
	FDelegateHandle OnDetectConsoleObjectUnregisteredHandle;

	/** A mapping of SetBy console variable flags to information like the associated display text. */
	static const inline TArray<FStaticConsoleVariableFlagInfo> SupportedFlags =
	{
		{ EConsoleVariableFlags::ECVF_SetByConstructor, LOCTEXT("Source_SetByConstructor", "Constructor") },
		{ EConsoleVariableFlags::ECVF_SetByScalability, LOCTEXT("Source_SetByScalability", "Scalability") },
		{ EConsoleVariableFlags::ECVF_SetByGameSetting, LOCTEXT("Source_SetByGameSetting", "Game Setting") },
		{ EConsoleVariableFlags::ECVF_SetByProjectSetting, LOCTEXT("Source_SetByProjectSetting", "Project Setting") },
		{ EConsoleVariableFlags::ECVF_SetBySystemSettingsIni, LOCTEXT("Source_SetBySystemSettingsIni", "System Settings ini") },
		{ EConsoleVariableFlags::ECVF_SetByDeviceProfile, LOCTEXT("Source_SetByDeviceProfile", "Device Profile") },
		{ EConsoleVariableFlags::ECVF_SetByGameOverride, LOCTEXT("Source_SetByGameOverride", "Game Override") },
		{ EConsoleVariableFlags::ECVF_SetByConsoleVariablesIni, LOCTEXT("Source_SetByConsoleVariablesIni", "Console Variables ini") },
		{ EConsoleVariableFlags::ECVF_SetByCommandline, LOCTEXT("Source_SetByCommandline", "Command line") },
		{ EConsoleVariableFlags::ECVF_SetByCode, LOCTEXT("Source_SetByCode", "Code") },
		{ EConsoleVariableFlags::ECVF_SetByConsole, LOCTEXT("Source_SetByConsole", "Console") }
	};
};

#undef LOCTEXT_NAMESPACE
