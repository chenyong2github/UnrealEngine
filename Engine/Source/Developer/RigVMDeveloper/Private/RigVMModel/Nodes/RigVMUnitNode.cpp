// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/Nodes/RigVMUnitNode.h"
#include "RigVMCore/RigVMStruct.h"

const FName URigVMUnitNode::LoopSliceContextName = TEXT("LoopContext");

FString URigVMUnitNode::GetNodeTitle() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetDisplayNameText().ToString();
	}
	return Super::GetNodeTitle();
}

FText URigVMUnitNode::GetToolTipText() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->GetToolTipText();
	}
	return URigVMNode::GetToolTipText();
}

bool URigVMUnitNode::IsDefinedAsConstant() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::ConstantMetaName);
	}
	return false;
}

bool URigVMUnitNode::IsDefinedAsVarying() const
{
	if (UScriptStruct* Struct = GetScriptStruct())
	{
		return Struct->HasMetaData(FRigVMStruct::VaryingMetaName);
	}
	return false;
}

FName URigVMUnitNode::GetEventName() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->GetEventName();
	}
	return NAME_None;
}

FName URigVMUnitNode::GetSliceContextForPin(URigVMPin* InRootPin, const FRigVMUserDataArray& InUserData)
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

int32 URigVMUnitNode::GetNumSlicesForContext(const FName& InContextName, const FRigVMUserDataArray& InUserData)
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

FText URigVMUnitNode::GetToolTipTextForPin(const URigVMPin* InPin) const
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

bool URigVMUnitNode::IsDeprecated() const
{
	return !GetDeprecatedMetadata().IsEmpty();
}

FString URigVMUnitNode::GetDeprecatedMetadata() const
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

UScriptStruct* URigVMUnitNode::GetScriptStruct() const
{
	return ScriptStruct;
}

bool URigVMUnitNode::IsLoopNode() const
{
	TSharedPtr<FStructOnScope> StructOnScope = ConstructStructInstance(true);
	if (StructOnScope.IsValid())
	{
		const FRigVMStruct* StructMemory = (FRigVMStruct*)StructOnScope->GetStructMemory();
		return StructMemory->IsForLoop();
	}
	return false;
}

FName URigVMUnitNode::GetMethodName() const
{
	return MethodName;
}

FString URigVMUnitNode::GetStructDefaultValue() const
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

TSharedPtr<FStructOnScope> URigVMUnitNode::ConstructStructInstance(bool bUseDefault) const
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
