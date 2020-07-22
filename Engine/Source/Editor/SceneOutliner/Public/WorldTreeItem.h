// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "ITreeItem.h"
#include "UObject/ObjectKey.h"

class UToolMenu;

namespace SceneOutliner
{
	/** Get a description of a world to display in the scene outliner */
	FText GetWorldDescription(UWorld* World);

	/** A tree item that represents an entire world */
	struct SCENEOUTLINER_API FWorldTreeItem : ITreeItem
	{
	public:
		DECLARE_DELEGATE_RetVal_OneParam(bool, FFilterPredicate, const UWorld*);
		DECLARE_DELEGATE_RetVal_OneParam(bool, FInteractivePredicate, const UWorld*);

		bool Filter(FFilterPredicate Pred) const
		{
			return Pred.Execute(World.Get());
		}

		bool GetInteractiveState(FInteractivePredicate Pred) const
		{
			return Pred.Execute(World.Get());
		}

		/** The world this tree item is associated with. */
		mutable TWeakObjectPtr<UWorld> World;
		
		/** Constant identifier for this tree item */
		const FObjectKey ID;

		/** Static type identifier for this tree item class */
		static const FTreeItemType Type;

		/** Construct this item from a world */
		FWorldTreeItem(UWorld* InWorld);
		FWorldTreeItem(TWeakObjectPtr<UWorld> InWorld);

		/* Begin ITreeItem Implementation */
		virtual bool IsValid() const override { return World.IsValid(); }
		virtual FTreeItemID GetID() const override;
		virtual FString GetDisplayString() const override;
		virtual bool CanInteract() const override;
		virtual void GenerateContextMenu(UToolMenu* Menu, SSceneOutliner& Outliner) override;
		virtual TSharedRef<SWidget> GenerateLabelWidget(ISceneOutliner& Outliner, const STableRow<FTreeItemPtr>& InRow) override;
		/* End ITreeItem Implementation */

		/** Open the world settings for the contained world */
		void OpenWorldSettings() const;
		/** Get just the name of the world, for tooltip use */
		FString GetWorldName() const;
	};

}		// namespace SceneOutliner
