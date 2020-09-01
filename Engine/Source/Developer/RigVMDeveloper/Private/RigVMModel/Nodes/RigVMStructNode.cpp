// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMStructNode.h"
#include "RigVMCore/RigVMStruct.h"

const FName URigVMStructNode::LoopSliceContextName = TEXT("LoopContext");

FString URigVMStructNode::GetNodeTitle() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetDisplayNameText().ToString();
	}
	return Super::GetNodeTitle();
}

FText URigVMStructNode::GetToolTipText() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetToolTipText();
	}
	return URigVMNode::GetToolTipText();
}

bool URigVMStructNode::IsDefinedAsConstant() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::ConstantMetaName);
	}
	return false;
}

bool URigVMStructNode::IsDefinedAsVarying() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::VaryingMetaName);
	}
	return false;
}

FName URigVMStructNode::GetEventName() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->GetEventName();
	}
	return NAME_None;
}

FName URigVMStructNode::GetSliceContextForPin(URigVMPin* InRootPin, const FRigVMUserDataArray& InUserData)
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(false);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		if (StructMemory->IsForLoop())
		{
			// if we are on any of the pins returning something from the loop
			if (InRootPin->GetFName() == FRigVMStruct::ExecuteContextName ||
				InRootPin->GetFName() == FRigVMStruct::ForLoopIndexPinName)
			{
				return LoopSliceContextName;
			}
			else if (InRootPin->GetFName() == FRigVMStruct::ForLoopCompletedPinName||
				InRootPin->GetFName() == FRigVMStruct::ForLoopCountPinName ||
				InRootPin->GetFName() == FRigVMStruct::ForLoopContinuePinName)
			{
				return NAME_None;
			}
		}
	}

	if (UScriptStruct* Struct = GetScriptStruct())
	{
		if (FProperty* Property = Struct->FindPropertyByName(InRootPin->GetFName()))
		{
			FString SliceContextMetaData = Property->GetMetaData(FRigVMStruct::SliceContextMetaName);
			if (!SliceContextMetaData.IsEmpty())
			{
				return *SliceContextMetaData;
			}
		}
	}

	return Super::GetSliceContextForPin(InRootPin, InUserData);
}

int32 URigVMStructNode::GetNumSlicesForContext(const FName& InContextName, const FRigVMUserDataArray& InUserData)
{
	int32 NumSlices = Super::GetNumSlicesForContext(InContextName, InUserData);

	if (InContextName == LoopSliceContextName)
	{
		TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(false);
		if (StructOnScope.IsValid())
		{
			const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
			if (StructMemory->IsForLoop())
			{
				NumSlices = NumSlices * FMath::Max<int32>(StructMemory->GetNumSlices(), 1);
			}
		}
	}

	return NumSlices;
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

bool URigVMStructNode::IsDeprecated() const
{
	return !GetDeprecatedMetadata().IsEmpty();
}

FString URigVMStructNode::GetDeprecatedMetadata() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		FString DeprecatedMetadata;
		if(Struct->GetStringMetaDataHierarchical(FRigVMStruct::DeprecatedMetaName, &DeprecatedMetadata))
		{
			return DeprecatedMetadata;
		}
	}
	return FString();
}

UScriptStruct* URigVMStructNode::GetScriptStruct() const
{
	return ScriptStruct;
}

bool URigVMStructNode::IsLoopNode() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->IsForLoop();
	}
	return false;
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
