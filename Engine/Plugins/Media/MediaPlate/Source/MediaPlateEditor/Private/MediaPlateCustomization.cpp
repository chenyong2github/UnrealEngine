// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "MediaPlate.h"
#include "MediaPlateEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
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
	ActorsList.Reserve(Objects.Num());
	for (TWeakObjectPtr<UObject>& Obj : Objects)
	{
		TWeakObjectPtr<AMediaPlate> ActorPtr = Cast<AMediaPlate>(Obj.Get());
		if (ActorPtr.IsValid())
		{
			ActorsList.Add(ActorPtr);
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
						.OnClicked_Lambda([this]() -> FReply
						{
							for (TWeakObjectPtr<AMediaPlate>& ActorPtr : ActorsList)
							{
								AMediaPlate* MediaPlate = ActorPtr.Get();
								if (MediaPlate != nullptr)
								{
									// Tell the editor module that this media plate is playing.
									FMediaPlateEditorModule* EditorModule = FModuleManager::LoadModulePtr<FMediaPlateEditorModule>("MediaPlateEditor");
									if (EditorModule != nullptr)
									{
										EditorModule->MediaPlateStartedPlayback(MediaPlate);
									}

									// Play the media.
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
						.OnClicked_Lambda([this]() -> FReply
						{
							for (TWeakObjectPtr<AMediaPlate>& ActorPtr : ActorsList)
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


	// Add button to open the media plate editor.
	MediaPlateCategory.AddCustomRow(LOCTEXT("OpenMediaPlate", "Open Media Plate"))
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.Padding(0, 5, 10, 5)
				[
					SNew(SButton)
						.ContentPadding(3)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.OnClicked(this, &FMediaPlateCustomization::OnOpenMediaPlate)
						.Text(LOCTEXT("OpenMediaPlate", "Open Media Plate"))
				]
		];
}

FReply FMediaPlateCustomization::OnOpenMediaPlate()
{
	// Get all our objects.
	TArray<UObject*> AssetArray;
	for (TWeakObjectPtr<AMediaPlate>& ActorPtr : ActorsList)
	{
		AMediaPlate* MediaPlate = ActorPtr.Get();
		if (MediaPlate != nullptr)
		{
			AssetArray.Add(MediaPlate);
		}
	}

	// Open the editor.
	if (AssetArray.Num() > 0)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(AssetArray);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
