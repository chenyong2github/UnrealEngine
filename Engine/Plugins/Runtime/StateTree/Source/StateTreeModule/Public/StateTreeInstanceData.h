// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InstancedStruct.h"
#include "StructView.h"
#include "StateTreeInstanceData.generated.h"

/**
 * StateTree instance data layout describes the layout of the instance data.
 */
struct STATETREEMODULE_API FStateTreeInstanceDataLayout
{
	/** Struct describing each item in the layout. */
	struct FLayoutItem
	{
		const UScriptStruct* ScriptStruct = nullptr;
		int32 Offset = 0;
	};
	
	/**
	 * Creates instance data layout from an array of structs.
	 * @return shared pointer to the layout
	 */
	static TSharedPtr<FStateTreeInstanceDataLayout> Create(TConstArrayView<const UScriptStruct*> Structs);

	/** @retrun the memory size required for the instance data. */
	int32 GetLayoutInstanceSize() const;
	/** @return the minimum alignment required by the instance data. */
	int32 GetLayoutInstanceMinAlignment() const;

	/** @return number if items in the layout. */
	int32 Num() const { return NumItems; }

	/** @return layout item at specified index */
	 FLayoutItem& GetMutableItem(const int32 Index) const
	{
		check(Index >= 0 && Index < NumItems);
		FLayoutItem* Items = GetItemsPtr();
		return Items[Index];
	}

	/** @return const layout item at specified index */
	const  FLayoutItem& GetItem(const int32 Index) const { return GetMutableItem(Index); }

private:
	FStateTreeInstanceDataLayout() = default;
	
	FLayoutItem* GetItemsPtr() const
	{
		static constexpr int32 ItemsOffset = Align(sizeof(FStateTreeInstanceDataLayout), alignof( FLayoutItem));
		return ( FLayoutItem*)((uint8*)(this) + ItemsOffset);
	}

	struct FLayoutMemoryDeleter
	{
		FORCEINLINE void operator()(FStateTreeInstanceDataLayout* Layout) const
		{
			FMemory::Free(Layout);
		}
	};

	/** Number of items in the layout. */
	int32 NumItems = 0;
};

/**
 * StateTree instance data is used to store the runtime state of a StateTree.
 * The layout of the data is described in a FStateTreeInstanceDataLayout.
 *
 * Note: Serialization is supported only for FArchive::IsModifyingWeakAndStrongReferences(), that is replacing object references.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeInstanceData
{
	GENERATED_BODY()

	FStateTreeInstanceData() = default;
	~FStateTreeInstanceData() { Reset(); }

	/** Creates new layout from an array if instanced structs, and copies the values into the instance data. */
	void Initialize(UObject& InOwner, TConstArrayView<FInstancedStruct> InValues, TConstArrayView<TObjectPtr<UObject>> InObjects);

	/** Shares the layout from another instance data, and copies the data over. */
	void CopyFrom(UObject& InOwner, const FStateTreeInstanceData& InOther);

	/** Resets the data to empty. */
	void Reset();

	/** @return Number of items in the instance data. */
	int32 Num() const { return Layout.IsValid() ? Layout->Num() : 0; }

	/** @return true if the instance is correctly initialized. */
	bool IsValid() const { return Memory != nullptr && Layout.IsValid(); }

	/** @return the layout of the instance data. */
	TSharedPtr<FStateTreeInstanceDataLayout> GetLayout() const { return Layout; };

	/** @return the script struct describing an item at specified index. */
	const UScriptStruct* GetScriptStruct(const int32 Index) const
	{
		check(IsValid());
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		return Item.ScriptStruct;
	}

	/** @returns mutable reference to the struct at specified index, this getter assumes that all data is valid. */
	template<typename T>
	T& GetMutable(const int32 Index) const
	{
		check(IsValid());
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		check(Item.ScriptStruct != nullptr);
		check(Item.ScriptStruct == T::StaticStruct() || Item.ScriptStruct->IsChildOf(T::StaticStruct()));
		return *((T*)(Memory + Item.Offset));
	}

	/** @returns mutable pointer to the struct at specified index, or nullptr if cast is not valid. */
	template<typename T>
	T* GetMutablePtr(const int32 Index) const
	{
		check(IsValid());
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		if (Item.ScriptStruct != nullptr && (Item.ScriptStruct == T::StaticStruct() || Item.ScriptStruct->IsChildOf(T::StaticStruct())))
		{
			return (T*)(Memory + Item.Offset);
		}
		return nullptr;
	}

	/** @return view to the struct at specified index. */
	FStructView GetMutable(const int32 Index) const
	{
		check(IsValid());
		const FStateTreeInstanceDataLayout::FLayoutItem& Item = Layout->GetItem(Index);
		return FStructView(Item.ScriptStruct, Memory + Item.Offset);
	}

	/** @returns const reference to the struct at specified index, this getter assumes that all data is valid. */
	template<typename T>
	const T& Get(const int32 Index) const { return GetMutable<T>(Index); }

	/** @returns const pointer to the struct at specified index, or nullptr if cast is not valid. */
	template<typename T>
	const T* GetPtr(const int32 Index) const { return GetMutablePtr<T>(Index); }

	/** @return const view to the struct at specified index. */
	FConstStructView Get(const int32 Index) const { return GetMutable(Index); }

	/** @return number of instance objects */
	int32 NumObjects() const { return InstanceObjects.Num(); }

	/** @return pointer to an instance object   */
	UObject* GetMutableObject(const int32 Index) const { return InstanceObjects[Index]; }

	/** @return const pointer to an instance object   */
	const UObject* GetObject(const int32 Index) const { return InstanceObjects[Index]; }

	int32 GetEstimatedMemoryUsage() const;
	int32 GetNumItems() const;
	
	/** Type traits */
	void AddStructReferencedObjects(class FReferenceCollector& Collector) const;
	bool Identical(const FStateTreeInstanceData* Other, uint32 PortFlags) const;
	bool Serialize(FArchive& Ar);
	
private:
	
	void Allocate(const TSharedPtr<FStateTreeInstanceDataLayout>& InLayout);

	/** Struct instances */
	uint8* Memory = nullptr;
	TSharedPtr<FStateTreeInstanceDataLayout> Layout;

	/** Object instances. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> InstanceObjects;
};

template<>
struct TStructOpsTypeTraits<FStateTreeInstanceData> : public TStructOpsTypeTraitsBase2<FStateTreeInstanceData>
{
	enum
	{
		WithSerializer = true,
		WithIdentical = true,
		WithAddStructReferencedObjects = true,
	};
};
