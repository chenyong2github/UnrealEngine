// Copyright Epic Games, Inc. All Rights Reserved.

#include "SequencerSpawnableColumn.h"
#include "ActorTreeItem.h"

namespace Sequencer
{

FName FSequencerSpawnableColumn::GetID()
{
	static FName IDName("Spawnable");
	return IDName;
}

FName FSequencerSpawnableColumn::GetColumnID()
{
	return GetID();
}

SHeaderRow::FColumn::FArguments FSequencerSpawnableColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column(GetColumnID())
		.FixedWidth(12.f)
		.HAlignHeader(HAlign_Left) // Note the icon has some baked in right alignment, so we are compensating here
		.VAlignHeader(VAlign_Center)
		.HAlignCell(HAlign_Center)
		.VAlignCell(VAlign_Center)
		.HeaderContentPadding(FMargin(0.0))
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Sequencer.SpawnableIconOverlay"))
		];
}

const TSharedRef< SWidget > FSequencerSpawnableColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	TSharedRef<SHorizontalBox> HorizontalBox = SNew(SHorizontalBox);

	HorizontalBox->AddSlot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.HAlign(HAlign_Center)
	[
		SNew(SImage)
		.Image(this, &FSequencerSpawnableColumn::GetSpawnableIcon, TreeItem)
	];

	return HorizontalBox;
}

const FSlateBrush* FSequencerSpawnableColumn::GetSpawnableIcon( FSceneOutlinerTreeItemRef TreeItem ) const
{
	static const FSlateBrush* SpawnedBrush  = FAppStyle::Get().GetBrush("Sequencer.SpawnableIconOverlay");
	static const FSlateBrush* NoSpawnedBrush  = FAppStyle::Get().GetBrush("NoBrush");

	bool bIsSpawned = false;
	{
		static const FName SequencerActorTag(TEXT("SequencerActor"));
		if (const FActorTreeItem* ActorItem = (*TreeItem).CastTo<FActorTreeItem>())
		{
			AActor* Actor = ActorItem->Actor.Get();
			bIsSpawned = Actor && Actor->ActorHasTag(SequencerActorTag);
		}
	}

	return bIsSpawned ? SpawnedBrush : NoSpawnedBrush;
}

}// End Sequencer namespace
