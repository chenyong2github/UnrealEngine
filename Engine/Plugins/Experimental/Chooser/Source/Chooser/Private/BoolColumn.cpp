// Copyright Epic Games, Inc. All Rights Reserved.
#include "BoolColumn.h"
#include "ChooserPropertyAccess.h"

bool UChooserParameterBool_ContextProperty::GetValue(const UObject* ContextObject, bool& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, PropertyBindingChain))
	{
		if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<bool>(Container);
			return true;
		}
	}

	return false;
}

UChooserColumnBool::UChooserColumnBool(const FObjectInitializer& ObjectInitializer)
{
	InputValue = ObjectInitializer.CreateDefaultSubobject<UChooserParameterBool_ContextProperty>(this, "InputValue");
	InputValue.GetObject()->SetFlags(RF_Transactional);
}

void UChooserColumnBool::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut)
{
	if (ContextObject && InputValue)
	{
		bool Result = false;
		InputValue->GetValue(ContextObject,Result);
		
		for (uint32 Index : IndexListIn)
		{
			if (RowValues.Num() > (int)Index)
			{
				if (Result == RowValues[Index])
				{
					IndexListOut.Push(Index);
				}
			}
		}
	}
	else
	{
		// passthrough fallback (behaves better during live editing)
		IndexListOut = IndexListIn;
	}
}