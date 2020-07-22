// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "Layout/Visibility.h"
#include "Input/DragAndDrop.h"
#include "DragAndDrop/CompositeDragDropOp.h"
#include "ITreeItem.h"

namespace SceneOutliner
{
	/** Enum to describe the compatibility of a drag drop operation */
	enum DropCompatibility
	{
		Drop_Compatible,
		Drop_Incompatible,
		Drop_MultipleSelection_Incompatible,
		Drop_CompatibleAttach,
		Drop_IncompatibleGeneric,
		Drop_CompatibleGeneric,
		Drop_CompatibleMultipleAttach,
		Drop_IncompatibleMultipleAttach,
		Drop_CompatibleDetach,
		Drop_CompatibleMultipleDetach
	};

	/** Consilidated drag/drop with parsing functions for the scene outliner */
	struct FDragDropPayload
	{
		/** Default constructor, resulting in unset contents */
		FDragDropPayload() {}

		/** Populate this payload from an array of tree items */
		template<typename TreeType>
		FDragDropPayload(const TArray<TreeType>& InDraggedItems)
		{
			for (const auto& Item : InDraggedItems)
			{
				DraggedItems.Add(Item);
			}
		}

		/** Returns true if the payload has an item of a specified type */
		template <typename TreeType>
		bool Has() const
		{
			for (const TWeakPtr<ITreeItem>& Item : DraggedItems)
			{
				if (const auto ItemPtr = Item.Pin())
				{
					if (ItemPtr->IsA<TreeType>())
					{
						return true;
					}
				}
			}
			return false;
		}

		/** Return an array of all tree items in the payload which are of a specified type */
		template <typename TreeType>
		TArray<TreeType*> Get() const
		{
			TArray<TreeType*> Result;
			for (const TWeakPtr<ITreeItem>& Item : DraggedItems)
			{
				if (const auto ItemPtr = Item.Pin())
				{
					if (TreeType* CastedItem = ItemPtr->CastTo<TreeType>())
					{
						Result.Add(CastedItem);
					}
				}
			}
			return Result;
		}

		/** Apply a function to each item in the payload */
		template <typename TreeType>
		void ForEachItem(TFunctionRef<void(TreeType&)> Func) const
		{
			for (const TWeakPtr<ITreeItem>& Item : DraggedItems)
			{
				if (const auto ItemPtr = Item.Pin())
				{
					if (TreeType* CastedItem = ItemPtr->CastTo<TreeType>())
					{
						Func(*CastedItem);
					}
				}
			}
		}
		
		/** Use a selector to retrieve an array of a specific data type from the items in the payload */
		template <typename DataType>
		TArray<DataType> GetData(TFunctionRef<bool(TWeakPtr<ITreeItem>, DataType&)> Selector) const
		{
			TArray<DataType> Result;
			for (TWeakPtr<ITreeItem>& Item : DraggedItems)
			{
				DataType Data;
				if (Selector(Item, Data))
				{
					Result.Add(Data);
				}
			}
			return Result;
		}

		/** List of all dragged items */
		mutable TArray<TWeakPtr<ITreeItem>> DraggedItems;
	};

	/** Struct used for validation of a drag/drop operation in the scene outliner */
	struct FDragValidationInfo
	{
		/** The tooltip type to display on the operation */
		DropCompatibility CompatibilityType;

		/** The tooltip text to display on the operation */
		FText ValidationText;

		/** Construct this validation information out of a tootip type and some text */
		FDragValidationInfo(const DropCompatibility InCompatibilityType, const FText InValidationText)
			: CompatibilityType(InCompatibilityType)
			, ValidationText(InValidationText)
		{}

		/** Return a generic invalid result */
		static FDragValidationInfo Invalid()
		{
			return FDragValidationInfo(DropCompatibility::Drop_IncompatibleGeneric, FText());
		}
		
		/** @return true if this operation is valid, false otheriwse */ 
		bool IsValid() const
		{
			switch(CompatibilityType)
			{
			case DropCompatibility::Drop_Compatible:
			case DropCompatibility::Drop_CompatibleAttach:
			case DropCompatibility::Drop_CompatibleGeneric:
			case DropCompatibility::Drop_CompatibleMultipleAttach:
			case DropCompatibility::Drop_CompatibleDetach:
			case DropCompatibility::Drop_CompatibleMultipleDetach:
				return true;
			default:
				return false;
			}
		}
	};

	/* A drag/drop operation when dragging folders in the scene outliner */
	struct FFolderDragDropOp: public FDecoratedDragDropOp
	{
		DRAG_DROP_OPERATOR_TYPE(FFolderDragDropOp, FDecoratedDragDropOp)

		/** Array of folders that we are dragging */
		FFolderPaths Folders;

		void Init(FFolderPaths InFolders);
	};

	/** A drag/drop operation that was started from the scene outliner */
	struct FSceneOutlinerDragDropOp : public FCompositeDragDropOp
	{
		DRAG_DROP_OPERATOR_TYPE(FSceneOutlinerDragDropOp, FDecoratedDragDropOp);
		
		FSceneOutlinerDragDropOp();

		using FDragDropOperation::Construct;

		void ResetTooltip()
		{
			OverrideText = FText();
			OverrideIcon = nullptr;
		}

		void SetTooltip(FText InOverrideText, const FSlateBrush* InOverrideIcon)
		{
			OverrideText = InOverrideText;
			OverrideIcon = InOverrideIcon;
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	private:

		EVisibility GetOverrideVisibility() const;
		EVisibility GetDefaultVisibility() const;

		FText OverrideText;
		FText GetOverrideText() const { return OverrideText; }

		const FSlateBrush* OverrideIcon;
		const FSlateBrush* GetOverrideIcon() const { return OverrideIcon; }
	};

}
