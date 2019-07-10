// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EditorMenuOwner.generated.h"


USTRUCT(BlueprintType, meta=(HasNativeBreak="EditorMenus.EditorMenuEntryExtensions.BreakEditorMenuOwner", HasNativeMake="EditorMenus.EditorMenuEntryExtensions.MakeEditorMenuOwner"))
struct EDITORMENUS_API FEditorMenuOwner
{
	GENERATED_BODY()

private:
	struct FStoreName
	{
		int32 Index;
		int32 Number;
	};

	enum class EValueType : uint8
	{
		None,
		Pointer,
		Name
	};

public:
	FORCEINLINE FEditorMenuOwner() : ValueInt64(0), ValueType(EValueType::None) {}
	FORCEINLINE FEditorMenuOwner(void* InPointer) : ValueInt64(reinterpret_cast<int64>(InPointer)), ValueType(EValueType::Pointer) {}

	FEditorMenuOwner(const WIDECHAR* InValue) : FEditorMenuOwner(FName(InValue)) {}
	FEditorMenuOwner(const ANSICHAR* InValue) : FEditorMenuOwner(FName(InValue)) {}

	FEditorMenuOwner(const FName InValue)
	{
		if (InValue == NAME_None)
		{
			ValueInt64 = 0;
			ValueType = EValueType::None;
		}
		else
		{
			ValueName.Index = InValue.GetComparisonIndex().ToUnstableInt();
			ValueName.Number = InValue.GetNumber();
			ValueType = EValueType::Name;
		}
	}

	FORCEINLINE bool operator==(const FEditorMenuOwner& Other) const
	{
		return Other.ValueInt64 == ValueInt64 && Other.ValueType == ValueType;
	}

	FORCEINLINE bool operator!=(const FEditorMenuOwner& Other) const
	{
		return Other.ValueInt64 != ValueInt64 || Other.ValueType != ValueType;
	}

	friend uint32 GetTypeHash(const FEditorMenuOwner& Key)
	{
		return GetTypeHash(Key.ValueInt64);
	}

	FORCEINLINE bool IsSet() const { return ValueInt64 != 0; }

	FName TryGetName() const
	{
		if (ValueType == EValueType::Name)
		{
			const FNameEntryId EntryId = FNameEntryId::FromUnstableInt(ValueName.Index);
			return FName(EntryId, EntryId, ValueName.Number);
		}

		return NAME_None;
	}

private:

	union
	{
		int64 ValueInt64;
		FStoreName ValueName;
	};

	EValueType ValueType;
};
