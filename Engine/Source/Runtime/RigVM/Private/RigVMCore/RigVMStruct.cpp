// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMStruct.h"
#include "UObject/StructOnScope.h"

const FName FRigVMStruct::DeprecatedMetaName("Deprecated");
const FName FRigVMStruct::InputMetaName("Input");
const FName FRigVMStruct::OutputMetaName("Output");
const FName FRigVMStruct::IOMetaName("IO");
const FName FRigVMStruct::HiddenMetaName("Hidden");
const FName FRigVMStruct::VisibleMetaName("Visible");
const FName FRigVMStruct::DetailsOnlyMetaName("DetailsOnly");
const FName FRigVMStruct::AbstractMetaName("Abstract");
const FName FRigVMStruct::CategoryMetaName("Category");
const FName FRigVMStruct::DisplayNameMetaName("DisplayName");
const FName FRigVMStruct::MenuDescSuffixMetaName("MenuDescSuffix");
const FName FRigVMStruct::ShowVariableNameInTitleMetaName("ShowVariableNameInTitle");
const FName FRigVMStruct::CustomWidgetMetaName("CustomWidget");
const FName FRigVMStruct::ConstantMetaName("Constant");
const FName FRigVMStruct::TitleColorMetaName("TitleColor");
const FName FRigVMStruct::NodeColorMetaName("NodeColor");
const FName FRigVMStruct::KeywordsMetaName("Keywords");
const FName FRigVMStruct::PrototypeNameMetaName("PrototypeName");
const FName FRigVMStruct::ExpandPinByDefaultMetaName("ExpandByDefault");
const FName FRigVMStruct::DefaultArraySizeMetaName("DefaultArraySize");
const FName FRigVMStruct::VaryingMetaName("Varying");
const FName FRigVMStruct::SingletonMetaName("Singleton");
const FName FRigVMStruct::SliceContextMetaName("SliceContext");
const FName FRigVMStruct::ExecuteName = TEXT("Execute");
const FName FRigVMStruct::ExecuteContextName = TEXT("ExecuteContext");
const FName FRigVMStruct::ForLoopCountPinName("Count");
const FName FRigVMStruct::ForLoopContinuePinName("Continue");
const FName FRigVMStruct::ForLoopCompletedPinName("Completed");
const FName FRigVMStruct::ForLoopIndexPinName("Index");

float FRigVMStruct::GetRatioFromIndex(int32 InIndex, int32 InCount)
{
	if (InCount <= 1)
	{
		return 0.f;
	}
	return ((float)FMath::Clamp<int32>(InIndex, 0, InCount - 1)) / ((float)(InCount - 1));
}

#if WITH_EDITOR

bool FRigVMStruct::ValidateStruct(UScriptStruct* InStruct, FString* OutErrorMessage)
{
	if (!InStruct->IsChildOf(StaticStruct()))
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = TEXT("Not a child of FRigVMStruct.");
		}
		return false;
	}

	FStructOnScope StructOnScope(InStruct);
	FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope.GetStructMemory();

	if (StructMemory->IsForLoop())
	{
		if (!CheckPinExists(InStruct, ForLoopCountPinName, TEXT("int32"), OutErrorMessage))
		{
			return false;
		}
		else
		{
			if (!CheckPinDirection(InStruct, ForLoopCountPinName, InputMetaName) &&
				!CheckPinDirection(InStruct, ForLoopCountPinName, OutputMetaName) &&
				!CheckPinDirection(InStruct, ForLoopCountPinName, HiddenMetaName))
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' pin needs to be either hidden, an input or an output."), *ForLoopCountPinName.ToString());
				}
				return false;
			}
			if (!CheckMetadata(InStruct, ForLoopCountPinName, SingletonMetaName, OutErrorMessage))
			{
				return false;
			}
		}

		if (!CheckPinExists(InStruct, ForLoopContinuePinName, TEXT("bool"), OutErrorMessage))
		{
			return false;
		}
		else
		{
			if (!CheckPinDirection(InStruct, ForLoopContinuePinName, HiddenMetaName))
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' pin needs to be hidden."), *ForLoopContinuePinName.ToString());
				}
				return false;
			}
			if (!CheckMetadata(InStruct, ForLoopContinuePinName, SingletonMetaName, OutErrorMessage))
			{
				return false;
			}
		}

		if (!CheckPinExists(InStruct, ForLoopIndexPinName, TEXT("int32"), OutErrorMessage))
		{
			return false;
		}
		else
		{
			if (!CheckPinDirection(InStruct, ForLoopIndexPinName, HiddenMetaName) &&
				!CheckPinDirection(InStruct, ForLoopIndexPinName, OutputMetaName))
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' pin needs to be hidden or an output."), *ForLoopIndexPinName.ToString());
				}
				return false;
			}
			if (!CheckMetadata(InStruct, ForLoopContinuePinName, SingletonMetaName, OutErrorMessage))
			{
				return false;
			}
		}
		
		if (!CheckPinExists(InStruct, ExecuteContextName, FString(), OutErrorMessage))
		{
			return false;
		}
		else
		{
			if (!CheckPinDirection(InStruct, ExecuteContextName, IOMetaName))
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' pin needs to be IO."), *ExecuteContextName.ToString());
				}
				return false;
			}
		}

		if (!CheckPinExists(InStruct, ForLoopCompletedPinName, FString(), OutErrorMessage))
		{
			return false;
		}
		else
		{
			if (!CheckPinDirection(InStruct, ForLoopCompletedPinName, OutputMetaName))
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' pin needs to be an output."), *ForLoopCompletedPinName.ToString());
				}
				return false;
			}
		}
	}

	return true;
}

bool FRigVMStruct::CheckPinDirection(UScriptStruct* InStruct, const FName& PinName, const FName& InDirectionMetaName)
{
	if (FProperty* Property = InStruct->FindPropertyByName(PinName))
	{
		if (InDirectionMetaName == IOMetaName)
		{
			return Property->HasMetaData(InputMetaName) && Property->HasMetaData(OutputMetaName);
		}
		else if (InDirectionMetaName == HiddenMetaName)
		{
			return !Property->HasMetaData(InputMetaName) && !Property->HasMetaData(OutputMetaName);
		}
		return Property->HasMetaData(InDirectionMetaName);
	}
	return true;
}

bool FRigVMStruct::CheckPinType(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType, FString* OutErrorMessage)
{
	if (FProperty* Property = InStruct->FindPropertyByName(PinName))
	{
		if (Property->GetCPPType() != ExpectedType)
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FString::Printf(TEXT("The '%s' property needs to be of type '%s'."), *ForLoopCountPinName.ToString(), *ExpectedType);;
			}
			return false;
		}
	}

	return true;
}

bool FRigVMStruct::CheckPinExists(UScriptStruct* InStruct, const FName& PinName, const FString& ExpectedType, FString* OutErrorMessage)
{
	if (FProperty* Property = InStruct->FindPropertyByName(PinName))
	{
		if (!ExpectedType.IsEmpty())
		{
			if (Property->GetCPPType() != ExpectedType)
			{
				if (OutErrorMessage)
				{
					*OutErrorMessage = FString::Printf(TEXT("The '%s' property needs to be of type '%s'."), *ForLoopCountPinName.ToString(), *ExpectedType);;
				}
				return false;
			}
		}
	}
	else
	{
		if (OutErrorMessage)
		{
			if (ExpectedType.IsEmpty())
			{
				*OutErrorMessage = FString::Printf(TEXT("Struct requires a '%s' property."), *ForLoopCountPinName.ToString());;
			}
			else
			{
				*OutErrorMessage = FString::Printf(TEXT("Struct requires a '%s' property of type '%s'."), *ForLoopCountPinName.ToString(), *ExpectedType);;
			}
		}
		return false;
	}

	return true;
}

bool FRigVMStruct::CheckMetadata(UScriptStruct* InStruct, const FName& PinName, const FName& InMetadataKey, FString* OutErrorMessage)
{
	if (FProperty* Property = InStruct->FindPropertyByName(PinName))
	{
		if (!Property->HasMetaData(InMetadataKey))
		{
			if (OutErrorMessage)
			{
				*OutErrorMessage = FString::Printf(TEXT("Property '%s' requires a '%s' metadata tag."), *PinName.ToString(), *InMetadataKey.ToString());;
			}
			return false;
		}
	}
	else
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Struct requires a '%s' property."), *PinName.ToString());;
		}
		return false;
	}

	return true;
}

bool FRigVMStruct::CheckFunctionExists(UScriptStruct* InStruct, const FName& FunctionName, FString* OutErrorMessage)
{
	FString Key = FString::Printf(TEXT("%s::%s"), *InStruct->GetStructCPPName(), *FunctionName.ToString());
	if (FRigVMRegistry::Get().FindFunction(*Key) == nullptr)
	{
		if (OutErrorMessage)
		{
			*OutErrorMessage = FString::Printf(TEXT("Function '%s' not found, required for this type of struct."), *Key);
		}
		return false;
	}
	return true;
}

ERigVMPinDirection FRigVMStruct::GetPinDirectionFromProperty(FProperty* InProperty)
{
	bool bIsInput = InProperty->HasMetaData(InputMetaName);
	bool bIsOutput = InProperty->HasMetaData(OutputMetaName);
	bool bIsVisible = InProperty->HasMetaData(VisibleMetaName);

	if (bIsVisible)
	{
		return ERigVMPinDirection::Visible;
	}
	
	if (bIsInput)
	{
		return bIsOutput ? ERigVMPinDirection::IO : ERigVMPinDirection::Input;
	} 
	
	if(bIsOutput)
	{
		return ERigVMPinDirection::Output;
	}

	return ERigVMPinDirection::Hidden;
}

FString FRigVMStruct::ExportToFullyQualifiedText(FProperty* InMemberProperty, const uint8* InMemberMemoryPtr)
{
	check(InMemberProperty);
	check(InMemberMemoryPtr);

	FString DefaultValue;

	if (FStructProperty* StructProperty = CastField<FStructProperty>(InMemberProperty))
	{
		DefaultValue = ExportToFullyQualifiedText(StructProperty->Struct, InMemberMemoryPtr);
	}
	else if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(InMemberProperty))
	{
		FScriptArrayHelper ScriptArrayHelper(ArrayProperty, InMemberMemoryPtr);

		TArray<FString> ElementValues;
		for (int32 ElementIndex = 0; ElementIndex < ScriptArrayHelper.Num(); ElementIndex++)
		{
			const uint8* ElementMemoryPtr = ScriptArrayHelper.GetRawPtr(ElementIndex);
			ElementValues.Add(ExportToFullyQualifiedText(ArrayProperty->Inner, ElementMemoryPtr));
		}

		if (ElementValues.Num() == 0)
		{
			DefaultValue = TEXT("()");
		}
		else
		{
			DefaultValue = FString::Printf(TEXT("(%s)"), *FString::Join(ElementValues, TEXT(",")));
		}
	}
	else
	{
		InMemberProperty->ExportTextItem(DefaultValue, InMemberMemoryPtr, nullptr, nullptr, PPF_None);

		if (CastField<FNameProperty>(InMemberProperty) != nullptr ||
			CastField<FStrProperty>(InMemberProperty) != nullptr)
		{
			if (DefaultValue.IsEmpty())
			{
				DefaultValue = TEXT("\"\"");
			}
			else
			{
				DefaultValue = FString::Printf(TEXT("\"%s\""), *DefaultValue);
			}
		}
	}

	return DefaultValue;
}

FString FRigVMStruct::ExportToFullyQualifiedText(UScriptStruct* InStruct, const uint8* InStructMemoryPtr)
{
	check(InStruct);
	check(InStructMemoryPtr);

	TArray<FString> FieldValues;
	for (TFieldIterator<FProperty> It(InStruct); It; ++It)
	{
		FString PropertyName = It->GetName();
		const uint8* StructMemberMemoryPtr = It->ContainerPtrToValuePtr<uint8>(InStructMemoryPtr);
		FString DefaultValue = ExportToFullyQualifiedText(*It, StructMemberMemoryPtr);
		FieldValues.Add(FString::Printf(TEXT("%s=%s"), *PropertyName, *DefaultValue));
	}

	if (FieldValues.Num() == 0)
	{
		return TEXT("()");
	}

	return FString::Printf(TEXT("(%s)"), *FString::Join(FieldValues, TEXT(",")));
}


#endif