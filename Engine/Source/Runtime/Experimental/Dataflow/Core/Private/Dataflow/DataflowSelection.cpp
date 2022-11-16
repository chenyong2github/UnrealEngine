// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSelection.h"

void FDataflowSelection::Initialize(int32 NumBits, bool Value) 
{ 
	SelectionArray.AddUninitialized(NumBits); 
	SelectionArray.SetRange(0, NumBits, Value); 
}

void FDataflowSelection::Initialize(const FDataflowSelection& Other)
{
	Initialize(Other.Num(), false);

	for (int32 Idx = 0; Idx < Other.Num(); ++Idx)
	{
		if (Other.IsSelected(Idx))
		{
			SelectionArray[Idx] = true;
		}
	}
}

void FDataflowSelection::AsArray(TArray<int32>& SelectionArr) const
{
	SelectionArr.Empty();

	TBitArray<>::FConstIterator It(SelectionArray);
	while (It)
	{
		int32 Index = It.GetIndex();

		if (SelectionArray[Index])
		{
			SelectionArr.Add(Index);
		}

		++It;
	}
}

void FDataflowSelection::SetFromArray(const TArray<int32>& SelectionArr)
{
	SelectionArray.Init(false, SelectionArray.Num());

	for (int32 Elem : SelectionArr)
	{
		SelectionArray[Elem] = true;
	}
}

void FDataflowSelection::AND(const FDataflowSelection& Other, FDataflowSelection& Result) const
{ 
	Result.SelectionArray = TBitArray<>::BitwiseAND(SelectionArray, Other.SelectionArray, EBitwiseOperatorFlags::MaxSize);
}

void FDataflowSelection::OR(const FDataflowSelection& Other, FDataflowSelection& Result) const
{
	Result.SelectionArray = TBitArray<>::BitwiseOR(SelectionArray, Other.SelectionArray, EBitwiseOperatorFlags::MaxSize);
}

void FDataflowSelection::XOR(const FDataflowSelection& Other, FDataflowSelection& Result) const
{
	Result.SelectionArray = TBitArray<>::BitwiseXOR(SelectionArray, Other.SelectionArray, EBitwiseOperatorFlags::MaxSize);
}

int32 FDataflowSelection::NumSelected() const
{
	int32 NumSelectedBits = 0;

	TBitArray<>::FConstIterator It(SelectionArray);
	while (It)
	{		 
		if (It.GetValue())
		{
			NumSelectedBits++;
		}

		++It;
	}

	return NumSelectedBits;
}

bool FDataflowSelection::AnySelected() const
{
	TBitArray<>::FConstIterator It(SelectionArray);
	while (It)
	{
		if (It.GetValue())
		{
			return true;
		}

		++It;
	}

	return false;
}
