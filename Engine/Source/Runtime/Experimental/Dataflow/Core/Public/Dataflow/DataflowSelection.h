// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataflowSelection.generated.h"

USTRUCT()
struct DATAFLOWCORE_API FDataflowSelection
{
	GENERATED_USTRUCT_BODY()

	void Initialize(int32 NumBits, bool Value);
	void Initialize(const FDataflowSelection& Other);
	int32 Num() const { return SelectionArray.Num(); }
	int32 NumSelected() const;
	bool AnySelected() const;
	bool IsSelected(int32 Idx) const { return SelectionArray[Idx]; }
	void SetSelected(int32 Idx) { SelectionArray[Idx] = true; }
	void SetNotSelected(int32 Idx) { SelectionArray[Idx] = false; }
	void AsArray(TArray<int32>& SelectionArr) const;
	void SetFromArray(const TArray<int32>& SelectionArr);
	void AND(const FDataflowSelection& Other, FDataflowSelection& Result) const;
	void OR(const FDataflowSelection& Other, FDataflowSelection& Result) const;
	void XOR(const FDataflowSelection& Other, FDataflowSelection& Result) const;
	void Invert() { SelectionArray.BitwiseNOT(); }
	void SetWithMask(const bool Value, const FDataflowSelection& Mask);
private:
	TBitArray<> SelectionArray;
};

USTRUCT()
struct DATAFLOWCORE_API FDataflowTransformSelection : public FDataflowSelection
{
	GENERATED_USTRUCT_BODY()
};
