// Copyright Epic Games, Inc. All Rights Reserved.
#include "BoolColumn.h"
#include "ChooserPropertyAccess.h"

bool FBoolContextProperty::GetValue(const UObject* ContextObject, bool& OutResult) const
{
	
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, Binding.PropertyBindingChain))
	{
		if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<bool>(Container);
			return true;
		}
		
	    if (UClass* ClassType = Cast<UClass>(StructType))
	    {
			if (UFunction* Function = ClassType->FindFunctionByName(Binding.PropertyBindingChain.Last()))
			{
				UObject* Object = reinterpret_cast<UObject*>(const_cast<void*>(Container));
				if (Function->IsNative())
				{
					FFrame Stack(Object, Function, nullptr, nullptr, Function->ChildProperties);
					Function->Invoke(Object, Stack, &OutResult);
				}
				else
				{
					Object->ProcessEvent(Function, &OutResult);
				}
			} 
		}
	}

	return false;
}

bool FBoolContextProperty::SetValue(UObject* ContextObject, bool InValue) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (UE::Chooser::ResolvePropertyChain(Container, StructType, Binding.PropertyBindingChain))
	{
		if (FBoolProperty* Property = FindFProperty<FBoolProperty>(StructType, Binding.PropertyBindingChain.Last()))
		{
			// const cast is here just because ResolvePropertyChain expects a const void*&
			*Property->ContainerPtrToValuePtr<bool>(const_cast<void*>(Container)) = InValue;
			return true;
		}
	}

	return false;
}

FBoolColumn::FBoolColumn()
{
	InputValue.InitializeAs(FBoolContextProperty::StaticStruct());
}

void FBoolColumn::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut) const
{
	if (ContextObject && InputValue.IsValid())
	{
		bool Result = false;
		InputValue.Get<FChooserParameterBoolBase>().GetValue(ContextObject,Result);
		
		for (uint32 Index : IndexListIn)
		{
			if (RowValuesWithAny.Num() > (int)Index)
			{
				
				if (RowValuesWithAny[Index] == EBoolColumnCellValue::MatchAny || Result == static_cast<bool>(RowValuesWithAny[Index]))
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