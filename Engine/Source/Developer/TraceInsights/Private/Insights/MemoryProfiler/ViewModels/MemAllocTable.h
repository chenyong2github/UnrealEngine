// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/MemoryProfiler/ViewModels/MemoryAlloc.h"
#include "Insights/Table/ViewModels/Table.h"

namespace Insights
{

class FTableColumn;

////////////////////////////////////////////////////////////////////////////////////////////////////

// Column identifiers
struct FMemAllocTableColumns
{
	static const FName StartTimeColumnId;
	static const FName EndTimeColumnId;
	static const FName DurationColumnId;
	static const FName AddressColumnId;
	static const FName CountColumnId;
	static const FName SizeColumnId;
	static const FName TagColumnId;
	static const FName FunctionColumnId;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemAllocTable : public FTable
{
public:
	FMemAllocTable();
	virtual ~FMemAllocTable();

	virtual void Reset();

	TArray<FMemoryAlloc>& GetAllocs() { return Allocs; }
	const TArray<FMemoryAlloc>& GetAllocs() const { return Allocs; }

	bool IsValidRowIndex(int32 InIndex) const { return InIndex >= 0 && InIndex < Allocs.Num(); }
	const FMemoryAlloc* GetMemAlloc(int32 InIndex) const { return IsValidRowIndex(InIndex) ? &Allocs[InIndex] : nullptr; }
	const FMemoryAlloc& GetMemAllocChecked(int32 InIndex) const { check(IsValidRowIndex(InIndex)); return Allocs[InIndex]; }

private:
	void AddDefaultColumns();

private:
	TArray<FMemoryAlloc> Allocs;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
