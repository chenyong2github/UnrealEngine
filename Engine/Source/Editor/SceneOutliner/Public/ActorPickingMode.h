// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ActorMode.h"

namespace SceneOutliner
{
	class FActorPickingMode : public FActorMode
	{
	public:
		FActorPickingMode(SSceneOutliner* InSceneOutliner, bool bInHideComponents, FOnSceneOutlinerItemPicked OnItemPickedDelegate, TWeakObjectPtr<UWorld> InSpecifiedWorldToDisplay = nullptr);

		virtual ~FActorPickingMode() {};
	public:
		virtual void OnItemSelectionChanged(FTreeItemPtr Item, ESelectInfo::Type SelectionType, const FItemSelection& Selection) override;

		/** Allow the user to commit their selection by pressing enter if it is valid */
		virtual void OnFilterTextCommited(FItemSelection& Selection, ETextCommit::Type CommitType) override;

		virtual void CreateViewContent(FMenuBuilder& MenuBuilder) override;
	public:
		virtual bool ShowViewButton() const override { return true; }
	private:
		FOnSceneOutlinerItemPicked OnItemPicked;
	};
}