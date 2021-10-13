// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ConsoleVariablesAsset.generated.h"

UENUM()
enum class EConsoleVariablesUiVariableType
{
	ConsoleVariablesType_Float,
	ConsoleVariablesType_Integer,
	ConsoleVariablesType_String,
	ConsoleVariablesType_Bool
};

USTRUCT()
struct FConsoleVariablesUiCommandInfo
{
	GENERATED_BODY()

	FConsoleVariablesUiCommandInfo()
	{
		ValueType = EConsoleVariablesUiVariableType::ConsoleVariablesType_String;
	};

	FConsoleVariablesUiCommandInfo(const FString& InCommand, const FString& InValueAsString, const EConsoleVariablesUiVariableType InValueType, const FString& InHelpText)
	: Command(InCommand)
	, ValueAsString((InValueAsString))
	, ValueType(InValueType)
	, HelpText(InHelpText)
	{};

	FORCEINLINE bool operator==(const FConsoleVariablesUiCommandInfo& Comparator) const
	{
		return ValueType == Comparator.ValueType && Command.Equals(Comparator.Command) && ValueAsString.Equals(Comparator.ValueAsString);
	}

	/** Just checks against Command Name and ValueType, not values. */
	FORCEINLINE bool SimilarTo(const FConsoleVariablesUiCommandInfo& Comparator) const
	{
		return ValueType == Comparator.ValueType && Command.Equals(Comparator.Command);
	}

	void SetValue(const int32 InValue, const bool bShouldExecute = false)
	{
		ValueAsString = FString::FromInt(InValue);

		if (bShouldExecute)
		{
			ExecuteCommand();
		}
	}

	void SetValue(const float InValue, const bool bShouldExecute = false)
	{
		ValueAsString = FString::SanitizeFloat(InValue);

		if (bShouldExecute)
		{
			ExecuteCommand();
		}
	}

	void SetValue(const FString& InValue, const bool bShouldExecute = false)
	{
		ValueAsString = InValue;

		if (bShouldExecute)
		{
			ExecuteCommand();
		}
	}

	void SetValue(const bool InValue, const bool bShouldExecute = false)
	{
		ValueAsString = InValue ? "1" : "0";

		if (bShouldExecute)
		{
			ExecuteCommand();
		}
	}

	void ExecuteCommand() const;

	UPROPERTY()
	FString Command;

	UPROPERTY()
	FString ValueAsString;

	UPROPERTY()
	EConsoleVariablesUiVariableType ValueType;

	FString HelpText;
};

/** An asset used to track collections of console variables that can be recalled and edited using the Console Variables UI. */
UCLASS(BlueprintType)
class CONSOLEVARIABLESEDITOR_API UConsoleVariablesAsset : public UObject
{
	GENERATED_BODY()
public:
	
	/** Sets the name of this variable collection. */
	UFUNCTION(BlueprintCallable, Category = "Console Variables UI")
	void SetVariableCollectionName(const FName& InVariableCollectionName);

	/** Sets a description for this variable collection. */
	UFUNCTION(BlueprintCallable, Category = "Console Variables UI")
	void SetVariableCollectionDescription(const FString& InVariableCollectionDescription);
	
	UFUNCTION(BlueprintPure, Category = "Console Variables UI")
	FName GetVariableCollectionName() const
	{
		return VariableCollectionName;
	}
	UFUNCTION(BlueprintPure, Category = "Console Variables UI")
	FString GetVariableCollectionDescription() const
	{
		return VariableCollectionDescription;
	}

	/** Returns the saved list of console variable information such as the variable name, the type and the value of the vriable at the time the asset was saved. */
	const TArray<FConsoleVariablesUiCommandInfo>& GetSavedCommandsAndValues() const
	{
		return SavedCommandsAndValues;
	}

	/** Returns how many console variables are serialized in this asset */
	int32 GetSavedCommandsAndValuesCount() const
	{
		return GetSavedCommandsAndValues().Num();
	}

	/** Outputs the FConsoleVariablesUiCommandInfo matching InCommand. Returns whether a match was found. Case sensitive. */
	bool FindCommandInfoByCommandString(const FString& InCommand, FConsoleVariablesUiCommandInfo& OutCommandInfo);

	/** Set the value of a saved console variable if the name and type match; add a new console variable to the list if a match is not found. */
	void AddOrSetConsoleVariableSavedValue(const FConsoleVariablesUiCommandInfo InCommandInfo);

	/** Returns true if the element was found and successfully removed. */
	bool RemoveConsoleVariable(const FConsoleVariablesUiCommandInfo InCommandInfo);

	/** Returns a text description of where the variable is set. */
	const FText& GetSource() const;
	/** A text description of where the variable is set. */
	void SetSource(const FText& InSourceText);

	/** Copy data from input asset to this asset */
	void CopyFrom(UConsoleVariablesAsset* InAssetToCopy);
	
private:

	/* User defined name for the variable collection, can differ from the actual asset name. */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetVariableCollectionName", EditAnywhere, Category = "Console Variables UI")
	FName VariableCollectionName;
	
	/* User defined description of the variable collection */
	UPROPERTY(AssetRegistrySearchable, BlueprintGetter = "GetVariableCollectionDescription", EditAnywhere, Category = "Console Variables UI")
	FString VariableCollectionDescription;

	/** A saved list of console variable information such as the variable name, the type and the value of the vriable at the time the asset was saved. */
	UPROPERTY()
	TArray<FConsoleVariablesUiCommandInfo> SavedCommandsAndValues;

	/** A text description of where the variable is set. */
	FText Source = NSLOCTEXT("ConsoleVariablesEditor", "ConsoleVariableSourceText", "Source");
};
