// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "UObject/ObjectKey.h"
#include "Templates/MaxSizeof.h"
#include "SceneOutlinerFwd.h"

namespace SceneOutliner
{
	using FTreeItemUniqueID = uint32;

	/** Variant type that defines an identifier for a tree item. Assumes 'trivial relocatability' as with many unreal containers. */
	struct FTreeItemID
	{
	public:
		enum class EType : uint8 { Object, Folder, UniqueID, Null };

		/** Default constructed null item ID */
		FTreeItemID() : Type(EType::Null), CachedHash(0) {}

		/** ID representing a UObject */
		FTreeItemID(const UObject* InObject) : Type(EType::Object)
		{
			check(InObject);
			new (Data) FObjectKey(InObject);
			CachedHash = CalculateTypeHash();
		}
		FTreeItemID(const FObjectKey& InKey) : Type(EType::Object)
		{
			new (Data) FObjectKey(InKey);
			CachedHash = CalculateTypeHash();
		}

		/** ID representing a folder */
		FTreeItemID(const FName& InFolder) : Type(EType::Folder)
		{
			new (Data) FName(InFolder);
			CachedHash = CalculateTypeHash();
		}

		/** ID representing a generic tree item */
		FTreeItemID(const FTreeItemUniqueID& CustomID) : Type(EType::UniqueID)
		{
			new (Data) FTreeItemUniqueID(CustomID);
			CachedHash = CalculateTypeHash();
		}

		/** Copy construction / assignment */
		FTreeItemID(const FTreeItemID& Other)
		{
			*this = Other;
		}
		FTreeItemID& operator=(const FTreeItemID& Other)
		{
			Type = Other.Type;
			switch(Type)
			{
				case EType::Object:			new (Data) FObjectKey(Other.GetAsObjectKey());		break;
				case EType::Folder:			new (Data) FName(Other.GetAsFolderRef());			break;
				case EType::UniqueID:		new (Data) FTreeItemUniqueID(Other.GetAsHash());	break;
				default:																		break;
			}

			CachedHash = CalculateTypeHash();
			return *this;
		}

		/** Move construction / assignment */
		FTreeItemID(FTreeItemID&& Other)
		{
			*this = MoveTemp(Other);
		}
		FTreeItemID& operator=(FTreeItemID&& Other)
		{
			FMemory::Memswap(this, &Other, sizeof(FTreeItemID));
			return *this;
		}

		~FTreeItemID()
		{
			switch(Type)
			{
				case EType::Object:			GetAsObjectKey().~FObjectKey();							break;
				case EType::Folder:			GetAsFolderRef().~FName();								break;
				case EType::UniqueID:		/* NOP */												break;
				default:																			break;
			}
		}

		friend bool operator==(const FTreeItemID& One, const FTreeItemID& Other)
		{
			return One.Type == Other.Type && One.CachedHash == Other.CachedHash && One.Compare(Other);
		}
		friend bool operator!=(const FTreeItemID& One, const FTreeItemID& Other)
		{
			return One.Type != Other.Type || One.CachedHash != Other.CachedHash || !One.Compare(Other);
		}

		uint32 CalculateTypeHash() const
		{
			uint32 Hash = 0;
			switch(Type)
			{
				case EType::Object:			Hash = GetTypeHash(GetAsObjectKey());				break;
				case EType::Folder:			Hash = GetTypeHash(GetAsFolderRef());				break;
				case EType::UniqueID:		Hash = GetAsHash();									break;
				default:																		break;
			}

			return HashCombine((uint8)Type, Hash);
		}

		friend uint32 GetTypeHash(const FTreeItemID& ItemID)
		{
			return ItemID.CachedHash;
		}

	private:

		FObjectKey& 				GetAsObjectKey() const 			{ return *reinterpret_cast<FObjectKey*>(Data); }
		FName& 						GetAsFolderRef() const			{ return *reinterpret_cast<FName*>(Data); }
		FTreeItemUniqueID&			GetAsHash() const				{ return *reinterpret_cast<FTreeItemUniqueID*>(Data); }

		/** Compares the specified ID with this one - assumes matching types */
		bool Compare(const FTreeItemID& Other) const
		{
			switch(Type)
			{
				case EType::Object:			return GetAsObjectKey() == Other.GetAsObjectKey();
				case EType::Folder:			return GetAsFolderRef() == Other.GetAsFolderRef();
				case EType::UniqueID:		return GetAsHash() == Other.GetAsHash();
				case EType::Null:			return true;
				default: check(false);		return false;
			}
		}

		EType Type;

		uint32 CachedHash;
		static const uint32 MaxSize = TMaxSizeof<FObjectKey, FName, FTreeItemUniqueID>::Value;
		mutable uint8 Data[MaxSize];
	};
	
	struct FTreeItemType
	{
	public:
		explicit FTreeItemType(const FTreeItemType* Parent = nullptr) : ID(++NextUniqueID), ParentType(Parent) {}
		FTreeItemType(const FTreeItemType& Src) : ID(Src.ID), ParentType(Src.ParentType) {}

		bool operator==(const FTreeItemType& Other) const
		{
			return ID == Other.ID || (ParentType != nullptr && *ParentType == Other);
		}

		bool IsA(const FTreeItemType& Other) const
		{
			return (ID == Other.ID) || (ParentType && ParentType->IsA(Other));
		}

	private:
		static uint32 NextUniqueID;
		uint32 ID;
		const FTreeItemType* ParentType;
	};

	struct FCommonLabelData
	{
		TWeakPtr<ISceneOutliner> WeakSceneOutliner;
		static const FLinearColor DarkColor;

		TOptional<FLinearColor> GetForegroundColor(const ITreeItem& TreeItem) const;

		bool CanExecuteRenameRequest(const ITreeItem& Item) const;
	};

	/**
	 * Contains hierarchy change data.
	 * When an item is added, it will contain a pointer to the new item itself.
	 * When an item is removed or moved, it will contain the unique ItemID to that item.
	 * In the case that a folder is being moved, it will also contain the new path to that folder.
	 */
	struct FHierarchyChangedData
	{
		enum
		{
			Added,
			Removed,
			Moved,
			FolderMoved,
			FullRefresh
		} Type;

		// This event may pass one of two kinds of data, depending on the type of event
		FTreeItemPtr Item;

		FTreeItemID ItemID;
		// Used for FolderMoved events
		FName NewPath;
		/** Actions to apply to items */
		uint8 ItemActions = 0;
	};

	/** Folder-specific helper functions */

	/** Parse a new path (including leaf-name) into this tree item. Does not do any notification */
	FName GetFolderLeafName(FName InPath);

	/** Get the parent path for the specified folder path */
	FORCEINLINE FName GetParentPath(FName Path)
	{
		return FName(*FPaths::GetPath(Path.ToString()));
	}

	bool PathIsChildOf(const FName& PotentialChild, const FName& Parent);
}	// namespace SceneOutliner
