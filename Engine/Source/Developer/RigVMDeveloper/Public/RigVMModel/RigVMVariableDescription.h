// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMCore/RigVMExternalVariable.h"
#include "RigVMModel/RigVMNode.h"
#include "EdGraphSchema_K2.h"
#include "RigVMVariableDescription.generated.h"

/**
 * The variable description is used to convey information
 * about unique variables within a Graph. Multiple Variable
 * Nodes can share the same variable description.
 */
USTRUCT(BlueprintType)
struct FRigVMGraphVariableDescription
{
	GENERATED_BODY()

public:

	// comparison operator
	bool operator ==(const FRigVMGraphVariableDescription& Other) const
	{
		return Name == Other.Name;
	}

	// The name of the variable
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FName Name;

	// The C++ data type of the variable
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString CPPType;

	// The Struct of the C++ data type of the variable (or nullptr)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	TObjectPtr<UObject> CPPTypeObject = nullptr;

	// The default value of the variable
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString DefaultValue;

	// Returns nullptr external variable matching this description
	FORCEINLINE FRigVMExternalVariable ToExternalVariable() const
	{
		FRigVMExternalVariable ExternalVariable;
		ExternalVariable.Name = Name;

		if (CPPType.StartsWith(TEXT("TArray<")))
		{
			ExternalVariable.bIsArray = true;
			ExternalVariable.TypeName = *CPPType.Mid(7, CPPType.Len() - 8);
			ExternalVariable.TypeObject = CPPTypeObject;
		}
		else
		{
			ExternalVariable.bIsArray = false;
			ExternalVariable.TypeName = *CPPType;
			ExternalVariable.TypeObject = CPPTypeObject;
		}

		ExternalVariable.bIsPublic = false;
		ExternalVariable.bIsReadOnly = false;
		ExternalVariable.Memory = nullptr;
		return ExternalVariable;
	}

	FORCEINLINE FEdGraphPinType ToPinType() const
	{
		FEdGraphPinType PinType;
		PinType.ResetToDefaults();
		PinType.PinCategory = NAME_None;

		FString CurrentCPPType = CPPType;
		if (CurrentCPPType.StartsWith(TEXT("TArray<")))
		{
			PinType.ContainerType = EPinContainerType::Array;
			CurrentCPPType.RemoveFromStart(TEXT("TArray<"));
			CurrentCPPType.RemoveFromEnd(TEXT(">"));
		}
		else
		{
			PinType.ContainerType = EPinContainerType::None;
		}

		if (CurrentCPPType == TEXT("bool"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (CurrentCPPType == TEXT("int32"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (CurrentCPPType == TEXT("float"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (CurrentCPPType == TEXT("double"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Double;
		}
		else if (CurrentCPPType == TEXT("FName"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (CurrentCPPType == TEXT("FString"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		}
		else if (Cast<UScriptStruct>(CPPTypeObject))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			PinType.PinSubCategoryObject = CPPTypeObject;
		}
		else if (Cast<UEnum>(CPPTypeObject))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			PinType.PinSubCategoryObject = CPPTypeObject;
		}
		else if (Cast<UClass>(CPPTypeObject))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = CPPTypeObject;
		}

		return PinType;
	}

	template <typename T>
	static bool CPPTypeFromPinType(const FEdGraphPinType& InPinType, FString& OutCPPType, T& OutCPPTypeObject)
	{
		FString Prefix = "";
		FString Suffix = "";
		if (InPinType.ContainerType == EPinContainerType::Array)
		{
			Prefix = TEXT("TArray<");
			Suffix = TEXT(">");
		}

		OutCPPTypeObject = nullptr;
		if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			OutCPPType = Prefix + TEXT("bool") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			OutCPPType = Prefix + TEXT("int32") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			OutCPPType = Prefix + TEXT("float") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Double)
		{
			OutCPPType = Prefix + TEXT("double") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			OutCPPType = Prefix + TEXT("FName") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			OutCPPType = Prefix + TEXT("FString") + Suffix;
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (UScriptStruct* Struct = Cast<UScriptStruct>(InPinType.PinSubCategoryObject))
			{
				OutCPPType = Prefix + *Struct->GetStructCPPName() + Suffix;
				OutCPPTypeObject = Struct;
			}
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
			InPinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
			InPinType.PinCategory == UEdGraphSchema_K2::AllObjectTypes)
		{
			if (UClass* Class = Cast<UClass>(InPinType.PinSubCategoryObject))
			{
				OutCPPType = Prefix + FString::Printf(TEXT("TObjectPtr<%s%s>"), Class->GetPrefixCPP(), *Class->GetName()) + Suffix;
				OutCPPTypeObject = Class;
			}
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Byte ||
			InPinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
		{
			if (UEnum* Enum = Cast<UEnum>(InPinType.PinSubCategoryObject))
			{
				OutCPPType = Prefix + Enum->GetFName().ToString() + Suffix;
				OutCPPTypeObject = Enum;
			}
			else
			{
				OutCPPType = Prefix + TEXT("uint8") + Suffix;
			}
		}
		else
		{
			return false;
		}
		
		return true;
	}

	FORCEINLINE bool ChangeType(const FEdGraphPinType& PinType)
	{
		return CPPTypeFromPinType(PinType, CPPType, CPPTypeObject);
	}
	
};
