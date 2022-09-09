// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"

UChooserTable::UChooserTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

}

void UChooserColumnBool::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut)
{
	bool Result = false;

	if (ContextObject)
	{
		if (const FBoolProperty* Property = FindFProperty<FBoolProperty>(ContextObject->GetClass(), InputPropertyName))
		{
			Result = *Property->ContainerPtrToValuePtr<bool>(ContextObject);
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
	}
}

void UChooserColumnFloatRange::Filter(const UObject* ContextObject, const TArray<uint32>& IndexListIn, TArray<uint32>& IndexListOut)
{
	float Result = 0.0f;

	Result = 0.0f;
	if (ContextObject)
	{
		if (const FDoubleProperty* Property = FindFProperty<FDoubleProperty>(ContextObject->GetClass(), InputPropertyName))
		{
			Result = *Property->ContainerPtrToValuePtr<double>(ContextObject);
	
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
			const TScriptInterface<IObjectChooser>& SelectedResult = Chooser->Results[SelectedIndex];

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
