// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeInstanceData.h"
#include "Algo/Transform.h"

//----------------------------------------------------------------//
//  FStateTreeInstanceDataLayout
//----------------------------------------------------------------//

TSharedPtr<FStateTreeInstanceDataLayout> FStateTreeInstanceDataLayout::Create(TConstArrayView<const UScriptStruct*> Structs)
{
	constexpr int32 MinAlignment = FMath::Max(alignof(FStateTreeInstanceDataLayout), alignof(FLayoutItem));

	const int NumStructs = Structs.Num();
		
	int32 RequiredSize = sizeof(FStateTreeInstanceDataLayout);
	RequiredSize = Align(RequiredSize, alignof(FLayoutItem)) + sizeof(FLayoutItem) * NumStructs;

	FStateTreeInstanceDataLayout* Layout = new(FMemory::Malloc(RequiredSize, MinAlignment)) FStateTreeInstanceDataLayout;

	Layout->NumItems = NumStructs;
	
	FLayoutItem* Items = Layout->GetItemsPtr();
	int32 Offset = 0;
	
	for (int32 ItemIndex = 0; ItemIndex < NumStructs; ItemIndex++)
	{
		FLayoutItem& Item = Items[ItemIndex];
		const UScriptStruct* ScriptStruct = Structs[ItemIndex];
		const int32 Alignment = ScriptStruct != nullptr ? ScriptStruct->GetMinAlignment() : 0;
		const int32 Size = ScriptStruct != nullptr ? ScriptStruct->GetStructureSize() : 0;

		Offset = Align(Offset, Alignment);

		Item.Offset = Offset;
		Item.ScriptStruct = ScriptStruct;
		
		Offset += Size;
	}
	
	return MakeShareable(Layout, FLayoutMemoryDeleter());
}

int32 FStateTreeInstanceDataLayout::GetLayoutInstanceSize() const
{
	if (NumItems == 0)
	{
		return 0;
	}
	
	const FLayoutItem* Items = GetItemsPtr();
	const FLayoutItem& Last = Items[NumItems - 1];

	return Last.Offset + (Last.ScriptStruct != nullptr ? Last.ScriptStruct->GetStructureSize() : 0);
}

int32 FStateTreeInstanceDataLayout::GetLayoutInstanceMinAlignment() const
{
	int32 CombinedAlignment = 0;
	
	const FLayoutItem* Items = GetItemsPtr();
	for (int32 Index = 0; Index < NumItems; Index++)
	{
		const FLayoutItem& Item = Items[Index];
		const int32 Alignment = Item.ScriptStruct != nullptr ? Item.ScriptStruct->GetMinAlignment() : 0;
		CombinedAlignment = FMath::Max(CombinedAlignment, Alignment);
	}
		
	return CombinedAlignment;
}

//----------------------------------------------------------------//
//  FStateTreeInstanceData
//----------------------------------------------------------------//

void FStateTreeInstanceData::AddStructReferencedObjects(class FReferenceCollector& Collector) const
{
	if (!Layout.IsValid())
	{
		return;
	}
	
	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		FStateTreeInstanceDataLayout::FLayoutItem Item = Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr)
		{
			Collector.AddReferencedObject(Item.ScriptStruct);
			Collector.AddReferencedObjects(Item.ScriptStruct, Memory + Item.Offset);
		}
	}
}

void FStateTreeInstanceData::Allocate(const TSharedPtr<FStateTreeInstanceDataLayout>& InLayout)
{
	Reset();
	
	check(InLayout.IsValid());
	Layout = InLayout;

	Memory = (uint8*)FMemory::Malloc(Layout->GetLayoutInstanceSize(), Layout->GetLayoutInstanceMinAlignment());

	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr)
		{
			Item.ScriptStruct->InitializeStruct(Memory + Item.Offset);
		}
	}
}

void FStateTreeInstanceData::CopyFrom(const FStateTreeInstanceData& InOther)
{
	if (&InOther == this)
	{
		return;
	}
	
	Allocate(InOther.GetLayout());

	// Copy values over
	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr)
		{
			Item.ScriptStruct->CopyScriptStruct(Memory + Item.Offset, InOther.Memory + Item.Offset);
		}
	}
}

void FStateTreeInstanceData::Initialize(TConstArrayView<FInstancedStruct> InValues)
{
	TArray<const UScriptStruct*> Structs;
	Algo::Transform(InValues, Structs, [](const FInstancedStruct& Value) { return Value.GetScriptStruct(); });
	
	Allocate(FStateTreeInstanceDataLayout::Create(Structs));

	// Copy values over
	check(Layout->Num() == InValues.Num());
	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		const FInstancedStruct& Value = InValues[Index];
		check(Item.ScriptStruct == Value.GetScriptStruct());
		if (Item.ScriptStruct != nullptr)
		{
			Item.ScriptStruct->CopyScriptStruct(Memory + Item.Offset, Value.GetMutableMemory());
		}
	}
}

void FStateTreeInstanceData::Reset()
{
	// Destruct items
	if (Layout.IsValid())
	{
		for (int32 Index = 0; Index < Layout->Num(); Index++)
		{
			const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
			if (Item.ScriptStruct != nullptr)
			{
				Item.ScriptStruct->DestroyStruct(Memory + Item.Offset);
			}
		}
	}

	// Free memory
	FMemory::Free(Memory);
	Memory = nullptr;
	Layout = nullptr;
}
