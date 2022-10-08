// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"

bool UChooserParameterBool_ContextProperty::GetValue(const UObject* ContextObject, bool& OutResult)
{
	if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(ContextObject->GetClass(), PropertyName))
	{
		OutResult = *Property->ContainerPtrToValuePtr<bool>(ContextObject);
	}
	
	return false;
}

bool UChooserParameterFloat_ContextProperty::GetValue(const UObject* ContextObject, float& OutResult)
{
	if (const FDoubleProperty* Property = FindFProperty<FDoubleProperty>(ContextObject->GetClass(), PropertyName))
	{
		OutResult = *Property->ContainerPtrToValuePtr<double>(ContextObject);
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

static UObject* StaticEvaluateChooser(const UObject* ContextObject, const UChooserTable* Chooser)
{
	if (Chooser == nullptr)
	{
		return nullptr;
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
			const IObjectChooser* SelectedResult = Chooser->Results[SelectedIndex].GetInterface();

			if(UObject* Result = SelectedResult->ChooseObject(ContextObject))
			{
				return Result;
			}
		}
	}

	return nullptr;
}

UObject* UObjectChooser_EvaluateChooser::ChooseObject(const UObject* ContextObject) const
{
	return StaticEvaluateChooser(ContextObject, Chooser);
}

UChooserFunctionLibrary::UChooserFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UObject* UChooserFunctionLibrary::EvaluateChooser(const UObject* ContextObject, const UChooserTable* Chooser)
{
	return StaticEvaluateChooser(ContextObject, Chooser);
}
