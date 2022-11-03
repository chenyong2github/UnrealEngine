// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"

UChooserTable::UChooserTable(const FObjectInitializer& Initializer)
	:Super(Initializer)
{

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
