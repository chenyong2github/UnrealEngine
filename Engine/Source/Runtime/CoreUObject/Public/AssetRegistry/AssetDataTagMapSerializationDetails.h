// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDataTagMap.h"

struct FAssetRegistrySerializationOptions;

namespace FixedTagPrivate
{
	/// Stores a fixed set of values and all the key-values maps used for lookup
	struct FStore
	{
		// Pairs for all unsorted maps that uses this store 
		TArrayView<FNumberedPair> Pairs;
		TArrayView<FNumberlessPair> NumberlessPairs;

		// Values for all maps in this store
		TArrayView<uint32> AnsiStringOffsets;
		TArrayView<ANSICHAR> AnsiStrings;
		TArrayView<uint32> WideStringOffsets;
		TArrayView<WIDECHAR> WideStrings;
		TArrayView<FNameEntryId> NumberlessNames;
		TArrayView<FName> Names;
		TArrayView<FNumberlessExportPath> NumberlessExportPaths;
		TArrayView<FAssetRegistryExportPath> ExportPaths;
		TArrayView<FText> Texts;

		const uint32 Index;
		void* Data = nullptr;

		void AddRef() const { RefCount.Increment(); }
		COREUOBJECT_API void Release() const;
		
		const ANSICHAR* GetAnsiString(uint32 Idx) const { return &AnsiStrings[AnsiStringOffsets[Idx]]; }
		const WIDECHAR* GetWideString(uint32 Idx) const { return &WideStrings[WideStringOffsets[Idx]]; }

	private:
		friend class FStoreManager;
		explicit FStore(uint32 InIndex) : Index(InIndex) {}
		~FStore();

		mutable FThreadSafeCounter RefCount;
	};

	struct FOptions
	{
		TSet<FName> StoreAsName;
		TSet<FName> StoreAsPath;
	};

	// Incomplete handle to a map in an unspecified FStore.
	// Used for serialization where the store index is implicit.
	struct COREUOBJECT_API alignas(uint64) FPartialMapHandle
	{
		bool bHasNumberlessKeys = false;
		uint16 Num = 0;
		uint32 PairBegin = 0;

		FMapHandle MakeFullHandle(uint32 StoreIndex) const;
		uint64 ToInt() const;
		static FPartialMapHandle FromInt(uint64 Int);
	};

	// Note: Can be changed to a single allocation and array views to improve cooker performance
	struct FStoreData
	{
		TArray<FNumberedPair> Pairs;
		TArray<FNumberlessPair> NumberlessPairs;

		TArray<uint32> AnsiStringOffsets;
		TArray<ANSICHAR> AnsiStrings;
		TArray<uint32> WideStringOffsets;
		TArray<WIDECHAR> WideStrings;
		TArray<FNameEntryId> NumberlessNames;
		TArray<FName> Names;
		TArray<FNumberlessExportPath> NumberlessExportPaths;
		TArray<FAssetRegistryExportPath> ExportPaths;
		TArray<FText> Texts;
	};

	uint32 HashCaseSensitive(const TCHAR* Str, int32 Len);
	uint32 HashCombineQuick(uint32 A, uint32 B);
	uint32 HashCombineQuick(uint32 A, uint32 B, uint32 C);

	// Helper class for saving or constructing an FStore
	class FStoreBuilder
	{
	public:
		explicit FStoreBuilder(const FOptions& InOptions) : Options(InOptions) {}
		explicit FStoreBuilder(FOptions&& InOptions) : Options(MoveTemp(InOptions)) {}

		COREUOBJECT_API FPartialMapHandle AddTagMap(const FAssetDataTagMapSharedView& Map);

		// Call once after all tag maps have been added
		COREUOBJECT_API FStoreData Finalize();

	private:

		template <typename ValueType>
		struct FCaseSensitiveFuncs : BaseKeyFuncs<ValueType, FString, /*bInAllowDuplicateKeys*/ false>
		{
			template<typename KeyType>
			static const KeyType& GetSetKey(const TPair<KeyType, ValueType>& Element)
			{
				return Element.Key;
			}

			static bool Matches(const FString& A, const FString& B)
			{
				return A.Equals(B, ESearchCase::CaseSensitive);
			}
			static uint32 GetKeyHash(const FString& Key)
			{
				return HashCaseSensitive(&Key[0], Key.Len());
			}

			static bool Matches(FNameEntryId A, FNameEntryId B)
			{
				return A == B;
			}
			static uint32 GetKeyHash(FNameEntryId Key)
			{
				return GetTypeHash(Key);
			}

			static bool Matches(FName A, FName B)
			{
				return (A.GetDisplayIndex() == B.GetDisplayIndex()) & (A.GetNumber() == B.GetNumber());
			}
			static uint32 GetKeyHash(FName Key)
			{
				return HashCombineQuick(GetTypeHash(Key.GetDisplayIndex()), Key.GetNumber());
			}

			template<class ExportPathType>
			static bool Matches(const ExportPathType& A, const ExportPathType& B)
			{
				return Matches(A.Class, B.Class) & Matches(A.Package, B.Package) & Matches(A.Object, B.Object);
			}

			template<class ExportPathType>
			static uint32 GetKeyHash(const ExportPathType& Key)
			{
				return HashCombineQuick(GetKeyHash(Key.Class), GetKeyHash(Key.Package), GetKeyHash(Key.Object));
			}
		};

		struct FStringIndexer
		{
			uint32 NumCharacters = 0;
			TMap<FString, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> StringIndices;
			TArray<uint32> Offsets;

			uint32 Index(FString&& String);

			TArray<ANSICHAR> FlattenAsAnsi() const;
			TArray<WIDECHAR> FlattenAsWide() const;
		};

		const FOptions Options;
		FStringIndexer AnsiStrings;
		FStringIndexer WideStrings;
		TMap<FNameEntryId, uint32> NumberlessNameIndices;
		TMap<FName, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> NameIndices;
		TMap<FNumberlessExportPath, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> NumberlessExportPathIndices;
		TMap<FAssetRegistryExportPath, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> ExportPathIndices;
		TMap<FString, uint32, FDefaultSetAllocator, FCaseSensitiveFuncs<uint32>> TextIndices;

		TArray<FNumberedPair> NumberedPairs;
		TArray<FNumberedPair> NumberlessPairs; // Stored as numbered for convenience

		bool bFinalized = false;

		FValueId IndexValue(FName Key, FAssetTagValueRef Value);
	};

	COREUOBJECT_API void SaveStore(const FStoreData& Store, FArchive& Ar);
	COREUOBJECT_API TRefCountPtr<const FStore> LoadStore(FArchive& Ar);

} // end namespace FixedTagPrivate