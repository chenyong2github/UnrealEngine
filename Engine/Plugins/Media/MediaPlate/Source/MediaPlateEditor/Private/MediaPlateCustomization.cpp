// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaPlateCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "MediaPlate.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "FMediaPlateCustomization"

/* IDetailCustomization interface
 *****************************************************************************/

void FMediaPlateCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& MediaPlateCategory = DetailBuilder.EditCategory("MediaPlate");

	// Get objects we are editing.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	TSharedPtr<TArray<TWeakObjectPtr<AMediaPlate>>> ActorsListPtr = MakeShared<TArray<TWeakObjectPtr<AMediaPlate>>>();
	ActorsListPtr->Reserve(Objects.Num());
	for (TWeakObjectPtr<UObject>& Obj : Objects)
	{
		TWeakObjectPtr<AMediaPlate> ActorPtr = Cast<AMediaPlate>(Obj.Get());
		if (ActorPtr.IsValid())
		{
			ActorsListPtr->Add(ActorPtr);
		}
	}

	// Add media control buttons.
	MediaPlateCategory.AddCustomRow(LOCTEXT("MediaPlateControls", "MediaPlate Controls"))
		.NameContent()
		[
			SNullWidget::NullWidget
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		.MaxDesiredWidth(250)
		[
			SNew(SHorizontalBox)

			// Play button.
			+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
						.VAlign(VAlign_Center)
						.OnClicked_Lambda([ActorsListPtr]() -> FReply
						{
							for (TWeakObjectPtr<AMediaPlate>& ActorPtr : *ActorsListPtr)
							{
								AMediaPlate* MediaPlate = ActorPtr.Get();
								if (MediaPlate != nullptr)
								{
									MediaPlate->Play();
								}
							}
							return FReply::Handled();
						})
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image(FEditorStyle::GetBrush("Icons.Toolbar.Play"))
						]
				]

			// Stop button.
			+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				[
					SNew(SButton)
						.VAlign(VAlign_Center)
						.OnClicked_Lambda([ActorsListPtr]() -> FReply
						{
							for (TWeakObjectPtr<AMediaPlate>& ActorPtr : *ActorsListPtr)
							{
								AMediaPlate* MediaPlate = ActorPtr.Get();
								if (MediaPlate != nullptr)
								{
									MediaPlate->Stop();
								}
							}
							return FReply::Handled();
						})
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image(FEditorStyle::GetBrush("Icons.Toolbar.Stop"))
						]
				]
		];

}

