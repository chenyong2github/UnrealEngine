// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMStructNode.h"

FText URigVMStructNode::GetToolTipText() const
{
	if(UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetToolTipText();
	}
	return URigVMNode::GetToolTipText();
}

FText URigVMStructNode::GetToolTipTextForPin(const URigVMPin* InPin) const
{
	if(UScriptStruct* Struct = GetScriptStruct())
	{
		TArray<FString> Parts;
		URigVMPin::SplitPinPath(InPin->GetPinPath(), Parts);

		for (int32 PartIndex = 1; PartIndex < Parts.Num(); PartIndex++)
		{
			FProperty* Property = Struct->FindPropertyByName(*Parts[PartIndex]);
			if (!Property)
			{
				break;
			}

			if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				if (PartIndex < Parts.Num() - 1)
				{
					Property = ArrayProperty->Inner;
					PartIndex++;
				}
			}

			if (PartIndex == Parts.Num() - 1)
			{
				return Property->GetToolTipText();
			}

			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				Struct = StructProperty->Struct;
			}
		}

	}
	return URigVMNode::GetToolTipTextForPin(InPin);
}

UScriptStruct* URigVMStructNode::GetScriptStruct() const
{
	return ScriptStruct;
}

FName URigVMStructNode::GetMethodName() const
{
	return MethodName;
}

FString URigVMStructNode::GetStructDefaultValue() const
{
	TArray<FString> PinDefaultValues;
	for (URigVMPin* Pin : GetPins())
	{
		if (Pin->GetDirection() == ERigVMPinDirection::Hidden)
		{
			continue;
		}
		FString PinDefaultValue = Pin->GetDefaultValue();
		if (Pin->IsStringType())
		{
			PinDefaultValue = TEXT("\"") + PinDefaultValue + TEXT("\"");
		}
		else if (PinDefaultValue.IsEmpty() || PinDefaultValue == TEXT("()"))
		{
			continue;
		}
		PinDefaultValues.Add(FString::Printf(TEXT("%s=%s"), *Pin->GetName(), *PinDefaultValue));
	}
	if (PinDefaultValues.Num() == 0)
	{
		return TEXT("()");
	}
	return FString::Printf(TEXT("(%s)"), *FString::Join(PinDefaultValues, TEXT(",")));

}

TSharedPtr<FStructOnScope> URigVMStructNode::ConstructStructInstance(bool bUseDefault) const
{
	if (ScriptStruct)
	{
		TSharedPtr<FStructOnScope> StructOnScope = MakeShareable(new FStructOnScope(ScriptStruct));
		FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		if (bUseDefault)
		{
			ScriptStruct->InitializeDefaultValue((uint8*)StructMemory);
		}
		else
		{
			FString StructDefaultValue = GetStructDefaultValue();
			ScriptStruct->ImportText(*StructDefaultValue, StructMemory, nullptr, PPF_None, nullptr, ScriptStruct->GetName());
		}
		return StructOnScope;
	}
	return nullptr;
}
