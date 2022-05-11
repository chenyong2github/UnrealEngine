// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeInstanceData.h"
#include "UObject/UnrealType.h"
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

int32 FStateTreeInstanceData::GetEstimatedMemoryUsage() const
{
	int32 Size = sizeof(FStateTreeInstanceData);

	if (Layout.IsValid())
	{
		Size = Layout->GetLayoutInstanceSize();
	}

	for (const UObject* InstanceObject : InstanceObjects)
	{
		if (InstanceObject)
		{
			Size += InstanceObject->GetClass()->GetStructureSize();
		}
	}

	return Size;
}

int32 FStateTreeInstanceData::GetNumItems() const
{
	int32 Num = InstanceObjects.Num();

	if (Layout.IsValid())
	{
		Num += Layout->Num();
	}
	
	return Num;
}

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

	for (const UObject* Object : InstanceObjects)
	{
		Collector.AddReferencedObject(Object);
	}	
}

bool FStateTreeInstanceData::Identical(const FStateTreeInstanceData* Other, uint32 PortFlags) const
{
	if (UNLIKELY(Other == nullptr))
	{
		return false;
	}

	// Identical if both are uninitialized.
	if (UNLIKELY(!IsValid() && !Other->IsValid()))
	{
		return true;
	}

	// Not identical if one is valid and other is not.
	if (UNLIKELY(IsValid() != Other->IsValid()))
	{
		return false;
	}

	// Not identical if different amount of instanced objects.
	if (UNLIKELY(InstanceObjects.Num() != Other->InstanceObjects.Num()))
	{
		return false;
	}

	// Not identical if different layouts.
	if (Layout != Other->Layout)
	{
		return false;
	}

	bool bResult = true;

	// Check that the struct contents are identical.
	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		const FStateTreeInstanceDataLayout::FLayoutItem Item = Layout->GetItem(Index);
		const FStateTreeInstanceDataLayout::FLayoutItem OtherItem = Other->Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr && OtherItem.ScriptStruct != nullptr)
		{
			check(Item.ScriptStruct == OtherItem.ScriptStruct);
			const uint8* ItemMemory = Memory + Item.Offset;
			const uint8* OtherItemMemory = Other->Memory + OtherItem.Offset;
			
			if (Item.ScriptStruct->CompareScriptStruct(ItemMemory, OtherItemMemory, PortFlags) == false)
			{
				bResult = false;
				break;
			}
		}
	}

	// Check that the instance object contents are identical.
	if (bResult)
	{
		// Copied from object property.
		auto AreObjectsIndentical = [](UObject* A, UObject* B, uint32 PortFlags) -> bool
		{
			if ((PortFlags & PPF_DuplicateForPIE) != 0)
			{
				return false;
			}

			if (A == B)
			{
				return true;
			}

			// Resolve the object handles and run the deep comparison logic 
			if ((PortFlags & (PPF_DeepCompareInstances | PPF_DeepComparison)) != 0)
			{
				return FObjectPropertyBase::StaticIdentical(A, B, PortFlags);
			}

			return true;
		};
		
		for (int32 Index = 0; Index < InstanceObjects.Num(); Index++)
		{
			if (InstanceObjects[Index] != nullptr && Other->InstanceObjects[Index] != nullptr)
			{
				if (!AreObjectsIndentical(InstanceObjects[Index], Other->InstanceObjects[Index], PortFlags))
				{
					bResult = false;
					break;
				}
			}
			else
			{
				bResult = false;
				break;
			}
		}
	}
	
	return bResult;
}

bool FStateTreeInstanceData::Serialize(FArchive& Ar)
{
	if (Ar.IsModifyingWeakAndStrongReferences())
	{
		// Just enough support to allow to replace a recompiled Blueprint.

		// Items
		if (Layout.IsValid())
		{
			for (int32 Index = 0; Index < Layout->Num(); ++Index)
			{
				const FStateTreeInstanceDataLayout::FLayoutItem Item = Layout->GetItem(Index);
				UScriptStruct* NonConstScriptStruct = const_cast<UScriptStruct*>(Item.ScriptStruct);
				if (NonConstScriptStruct != nullptr)
				{
					NonConstScriptStruct->SerializeItem(Ar, Memory + Item.Offset, nullptr);
				}
			}
		}

		// Instances
		Ar << InstanceObjects;
	}

	return true;
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

void FStateTreeInstanceData::CopyFrom(UObject& InOwner, const FStateTreeInstanceData& InOther)
{
	if (&InOther == this)
	{
		return;
	}
	
	Allocate(InOther.GetLayout());

	// Copy struct values over
	for (int32 Index = 0; Index < Layout->Num(); Index++)
	{
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr)
		{
			Item.ScriptStruct->CopyScriptStruct(Memory + Item.Offset, InOther.Memory + Item.Offset);
		}
	}

	// Copy instance objects.
	InstanceObjects.Reset();
	for (const UObject* Instance : InOther.InstanceObjects)
	{
		if (ensure(Instance != nullptr))
		{
			ensure(Instance->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists) == false);
			InstanceObjects.Add(DuplicateObject(Instance, &InOwner));
		}
	}
}

void FStateTreeInstanceData::Initialize(UObject& InOwner, TConstArrayView<FInstancedStruct> InValues, TConstArrayView<TObjectPtr<UObject>> InObjects)
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

	// Copy UObjects
	InstanceObjects.Reset();
	for (const UObject* Instance : InObjects)
	{
		if (ensure(Instance != nullptr))
		{
			ensure(Instance->GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists) == false);
			InstanceObjects.Add(DuplicateObject(Instance, &InOwner));
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

	InstanceObjects.Reset();
}
