// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"

bool ResolvePropertyChain(const void*& Container, UStruct*& StructType, const TArray<FName>& PropertyBindingChain)
{
	if (PropertyBindingChain.IsEmpty())
	{
		return false;
	}
	
	const int PropertyChainLength = PropertyBindingChain.Num();
	for(int PropertyChainIndex = 0; PropertyChainIndex < PropertyChainLength - 1; PropertyChainIndex++)
	{
		if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(StructType, PropertyBindingChain[PropertyChainIndex]))
		{
			StructType = StructProperty->Struct;
			Container = StructProperty->ContainerPtrToValuePtr<void>(Container);
		}
		else if (const FObjectProperty* ObjectProperty = FindFProperty<FObjectProperty>(StructType, PropertyBindingChain[PropertyChainIndex]))
		{
			StructType = ObjectProperty->PropertyClass;
			Container = *ObjectProperty->ContainerPtrToValuePtr<TObjectPtr<UObject>>(Container);
			if (Container == nullptr)
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	
	return true;
}

bool UChooserParameterBool_ContextProperty::GetValue(const UObject* ContextObject, bool& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (ResolvePropertyChain(Container, StructType, PropertyBindingChain))
	{
		if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(StructType, PropertyBindingChain.Last()))
		{
			OutResult = *Property->ContainerPtrToValuePtr<bool>(Container);
			return true;
		}
	}

	return false;
}

bool UChooserParameterFloat_ContextProperty::GetValue(const UObject* ContextObject, float& OutResult) const
{
	UStruct* StructType = ContextObject->GetClass();
	const void* Container = ContextObject;
	
	if (ResolvePropertyChain(Container, StructType, PropertyBindingChain))
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

UChooserTable::UChooserTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

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

static IObjectChooser::EIteratorStatus StaticEvaluateChooser(const UObject* ContextObject, const UChooserTable* Chooser, IObjectChooser::FObjectChooserIteratorCallback Callback)
{
	if (Chooser == nullptr)
	{
		return IObjectChooser::EIteratorStatus::Continue;
	}

	TArray<uint32> Indices1;
	TArray<uint32> Indices2;

	int RowCount = Chooser->Results.Num();
	Indices1.SetNum(RowCount);
	for(int i=0;i<RowCount;i++)
	{
		Indices1[i]=i;
	}
	TArray<uint32>* IndicesOut = &Indices1;
	TArray<uint32>* IndicesIn = &Indices2;

	for (auto Column: Chooser->Columns)
	{
		if (Column)
		{
			Swap(IndicesIn, IndicesOut);
			IndicesOut->SetNum(0, false);
			Column->Filter(ContextObject, *IndicesIn, *IndicesOut);
		}
	}

	// of the rows that passed all column filters, return the first one for which the result row succeeds (could fail eg for a nexted chooser where no rows passed)
	for (uint32 SelectedIndex : *IndicesOut)
	{
		if (Chooser->Results.Num() > (int32)SelectedIndex)
		{
			if (const IObjectChooser* SelectedResult = Chooser->Results[SelectedIndex].GetInterface())
			{
				if (SelectedResult->ChooseMulti(ContextObject, Callback) == IObjectChooser::EIteratorStatus::Stop)
				{
					return IObjectChooser::EIteratorStatus::Stop;
				}
			}
		}
	}
	
	return IObjectChooser::EIteratorStatus::Continue;
}

UObject* UObjectChooser_EvaluateChooser::ChooseObject(const UObject* ContextObject) const
{
	UObject* Result = nullptr;
	StaticEvaluateChooser(ContextObject, Chooser, FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
	{
		Result = InResult;
		return IObjectChooser::EIteratorStatus::Stop;
	}));

	return Result;
}

IObjectChooser::EIteratorStatus UObjectChooser_EvaluateChooser::ChooseMulti(const UObject* ContextObject, FObjectChooserIteratorCallback Callback) const
{
	return StaticEvaluateChooser(ContextObject, Chooser, Callback);
}

UChooserFunctionLibrary::UChooserFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UObject* UChooserFunctionLibrary::EvaluateChooser(const UObject* ContextObject, const UChooserTable* Chooser)
{
	UObject* Result = nullptr;
	StaticEvaluateChooser(ContextObject, Chooser, IObjectChooser::FObjectChooserIteratorCallback::CreateLambda([&Result](UObject* InResult)
	{
		Result = InResult;
		return IObjectChooser::EIteratorStatus::Stop;
	}));

	return Result;

	return Result;
}
