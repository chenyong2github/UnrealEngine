// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Views/TableViewTypeTraits.h"

namespace UE::MVVM
{
	struct FBindingSource
	{
		FGuid SourceGuid;
		FName Name;
		const UClass* Class = nullptr;
		FText DisplayName;
		bool IsSelected = false;

		bool operator==(const FBindingSource& Other) const
		{
			return SourceGuid == Other.SourceGuid && Name == Other.Name && Class == Other.Class;
		}

		bool operator!=(const FBindingSource& Other) const
		{
			return !(operator==(Other));
		}

		friend int32 GetTypeHash(const FBindingSource& Source)
		{
			uint32 Hash = HashCombine(GetTypeHash(Source.SourceGuid), GetTypeHash(Source.Name));
			Hash = HashCombine(Hash, GetTypeHash(Source.Class));
			return Hash;
		}

		bool IsValid() const
		{
			return SourceGuid.IsValid() || !Name.IsNone();
		}

		void Reset()
		{
			SourceGuid = FGuid();
			Name = FName();
			Class = nullptr;
			DisplayName = FText::GetEmpty();
			IsSelected = false;
		}
	};
}

template <>
struct TIsValidListItem<UE::MVVM::FBindingSource>
{
	enum
	{
		Value = true
	};
};

template <>
struct TListTypeTraits<UE::MVVM::FBindingSource>
{
	typedef UE::MVVM::FBindingSource NullableType;

	using MapKeyFuncs = TDefaultMapHashableKeyFuncs<UE::MVVM::FBindingSource, TSharedRef<ITableRow>, false>;
	using MapKeyFuncsSparse = TDefaultMapHashableKeyFuncs<UE::MVVM::FBindingSource, FSparseItemInfo, false>;
	using SetKeyFuncs = DefaultKeyFuncs<UE::MVVM::FBindingSource>;

	template<typename U>
	static void AddReferencedObjects(FReferenceCollector&,
		TArray<UE::MVVM::FBindingSource>&,
		TSet<UE::MVVM::FBindingSource>&,
		TMap<const U*, UE::MVVM::FBindingSource>&)
	{
	}

	static bool IsPtrValid(const UE::MVVM::FBindingSource& InPtr)
	{
		return InPtr.IsValid();
	}

	static void ResetPtr(UE::MVVM::FBindingSource& InPtr)
	{
		InPtr.Reset();
	}

	static UE::MVVM::FBindingSource MakeNullPtr()
	{
		return UE::MVVM::FBindingSource();
	}

	static UE::MVVM::FBindingSource NullableItemTypeConvertToItemType(const UE::MVVM::FBindingSource& InPtr)
	{
		return InPtr;
	}

	static FString DebugDump(UE::MVVM::FBindingSource InPtr)
	{
		return InPtr.DisplayName.ToString();
	}

	class SerializerType {};
};
