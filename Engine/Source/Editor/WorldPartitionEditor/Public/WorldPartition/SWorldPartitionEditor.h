// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "WorldPartition/WorldPartition.h"

class SWorldPartitionEditor : public SCompoundWidget, public IWorldPartitionEditor
{
public:
	SLATE_BEGIN_ARGS(SWorldPartitionEditor)
		:_InWorld(nullptr)
		{}
		SLATE_ARGUMENT(UWorld*, InWorld)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	~SWorldPartitionEditor();

	// IWorldPartitionEditor interface
	virtual void Refresh() override;

private:
	void OnBrowseWorld(UWorld* InWorld);

	TSharedRef<SWidget> ConstructContentWidget();

	FText GetMouseLocationText() const;

	FDelegateHandle WorldPartitionChangedDelegateHandle;
	TSharedPtr<SBorder>	ContentParent;
	TSharedPtr<class SWorldPartitionEditorGrid> GridView;
	UWorld* World;
};