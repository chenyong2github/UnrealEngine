// Copyright Epic Games, Inc. All Rights Reserved.
#include "FloatRangeColumn.h"
#include "ChooserPropertyAccess.h"

bool FFloatContextProperty::GetValue(const UObject* ContextObject, float& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;

	if (UE::Chooser::ResolvePropertyChain(Container, StructType, Binding.PropertyBindingChain))
	{
		if (const FDoubleProperty* DoubleProperty = FindFProperty<FDoubleProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			OutResult = *DoubleProperty->ContainerPtrToValuePtr<double>(Container);
			return true;
		}
		
		if (const FFloatProperty* FloatProperty = FindFProperty<FFloatProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			OutResult = *FloatProperty->ContainerPtrToValuePtr<float>(Container);
			return true;
		}

	    if (UClass* ClassType = Cast<UClass>(StructType))
	    {
			if (UFunction* Function = ClassType->FindFunctionByName(Binding.PropertyBindingChain.Last()))
			{
				bool bReturnsDouble = CastField<FDoubleProperty>(Function->GetReturnProperty()) != nullptr;
					
				UObject* Object = reinterpret_cast<UObject*>(const_cast<void*>(Container));
				if (Function->IsNative())
				{
					FFrame Stack(Object, Function, nullptr, nullptr, Function->ChildProperties);
					if (bReturnsDouble)
					{
						double result;
						Function->Invoke(Object, Stack, &result);
						OutResult = result;
					}
					else
					{
						Function->Invoke(Object, Stack, &OutResult);
					}
				}
				else
				{
					if (bReturnsDouble)
					{
						double result = 0;
						Object->ProcessEvent(Function, &result);
						OutResult = result;
					}
					else
					{
						Object->ProcessEvent(Function, &OutResult);
					}
				}
				return true;
			} 
		}
	}

	return false;
}

FFloatRangeColumn::FFloatRangeColumn()
{
	InputValue.InitializeAs(FFloatContextProperty::StaticStruct());
}

void FFloatRangeColumn::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) const
{
	if (ContextObject && InputValue.IsValid())
	{
		float Result = 0.0f;
		InputValue.Get<FChooserParameterFloatBase>().GetValue(ContextObject, Result);

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