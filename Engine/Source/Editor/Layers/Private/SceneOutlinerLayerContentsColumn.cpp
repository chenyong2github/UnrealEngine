// Copyright Epic Games, Inc. All Rights Reserved.


#include "SceneOutlinerLayerContentsColumn.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"

#include "ActorTreeItem.h"

#define LOCTEXT_NAMESPACE "SceneOutlinerLayerContentsColumn"


FSceneOutlinerLayerContentsColumn::FSceneOutlinerLayerContentsColumn( const TSharedRef< FLayerViewModel >& InViewModel )
	: ViewModel( InViewModel )
{

}

FName FSceneOutlinerLayerContentsColumn::GetID()
{
	return FName( "LayerContents" );
}

FName FSceneOutlinerLayerContentsColumn::GetColumnID()
{
	return GetID();
}


SHeaderRow::FColumn::FArguments FSceneOutlinerLayerContentsColumn::ConstructHeaderRowColumn()
{
	return SHeaderRow::Column( GetColumnID() )
		.FillWidth( 2.f )
		[
			SNew( SSpacer )
		];
}

const TSharedRef<SWidget> FSceneOutlinerLayerContentsColumn::ConstructRowWidget(FSceneOutlinerTreeItemRef TreeItem, const STableRow<FSceneOutlinerTreeItemPtr>& Row)
{
	if (FActorTreeItem* ActorItem = TreeItem->CastTo<FActorTreeItem>())
	{
		return SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ButtonStyle(FEditorStyle::Get(), "LayerBrowserButton")
			.ContentPadding(0)
			.OnClicked(this, &FSceneOutlinerLayerContentsColumn::OnRemoveFromLayerClicked, ActorItem->Actor)
			.ToolTipText(LOCTEXT("RemoveFromLayerButtonText", "Remove from Layer"))
			[
				SNew(SImage)
				.Image(FEditorStyle::GetBrush(TEXT("LayerBrowser.Actor.RemoveFromLayer")))
			];
	}
	return SNullWidget::NullWidget;
}

FReply FSceneOutlinerLayerContentsColumn::OnRemoveFromLayerClicked( const TWeakObjectPtr< AActor > Actor )
{
	ViewModel->RemoveActor( Actor );
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
