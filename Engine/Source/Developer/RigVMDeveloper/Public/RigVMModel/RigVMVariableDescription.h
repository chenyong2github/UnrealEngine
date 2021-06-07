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
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FName Name;

	// The C++ data type of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	FString CPPType;

	// The Struct of the C++ data type of the variable (or nullptr)
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
	UObject* CPPTypeObject = nullptr;

	// The default value of the variable
	UPROPERTY(BlueprintReadOnly, Category = RigVMGraphVariableDescription)
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

	FORCEINLINE FEdGraphPinType ToPinType()
	{
		FEdGraphPinType PinType;
		PinType.ResetToDefaults();
		PinType.PinCategory = NAME_None;

		if (CPPType == TEXT("bool"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		}
		else if (CPPType == TEXT("int32"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		}
		else if (CPPType == TEXT("float"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Float;
		}
		else if (CPPType == TEXT("FName"))
		{
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		}
		else if (CPPType == TEXT("FString"))
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

		if (CPPType.StartsWith(TEXT("TArray<")))
		{
			PinType.ContainerType = EPinContainerType::Array;
		}
		else
		{
			PinType.ContainerType = EPinContainerType::None;
		}

		return PinType;
	}

	static bool CPPTypeFromPinType(const FEdGraphPinType& InPinType, FString& OutCPPType, UObject*& OutCPPTypeObject)
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
			OutCPPType = Prefix + TEXT("struct") + Suffix;
			OutCPPTypeObject = InPinType.PinSubCategoryObject.Get();
		}
		else if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		{
			OutCPPType = Prefix + TEXT("byte") + Suffix;
			OutCPPTypeObject = InPinType.PinSubCategoryObject.Get();
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
