// Copyright Epic Games, Inc. All Rights Reserved.
#include "FloatRangeColumn.h"
#include "ChooserPropertyAccess.h"

bool UChooserParameterFloat_ContextProperty::GetValue(const UObject* ContextObject, float& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, PropertyBindingChain))
	{
		if (const FDoubleProperty* DoubleProperty = FindFProperty<FDoubleProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *DoubleProperty->ContainerPtrToValuePtr<double>(Container);
			return true;
		}
		
		if (const FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *FloatProperty->ContainerPtrToValuePtr<float>(Container);
			return true;
		}
	}

	return false;
}

UChooserColumnFloatRange::UChooserColumnFloatRange(const FObjectInitializer& ObjectInitializer)
{
	InputValue = ObjectInitializer.CreateDefaultSubobject<UChooserParameterFloat_ContextProperty>(this, "InputValue");
	InputValue.GetObject()->SetFlags(RF_Transactional);
}

void UChooserColumnFloatRange::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut)
{
	if (ContextObject && InputValue)
	{
		float Result = 0.0f;
		InputValue->GetValue(ContextObject, Result);

		for(uint32 Index : IndexListIn)
		{
			if (RowValues.Num() > (int)Index)
			{
				const FChooserFloatRangeRowData& RowValue = RowValues[Index];
				if (Result >= RowValue.Min && Result <= RowValue.Max)
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