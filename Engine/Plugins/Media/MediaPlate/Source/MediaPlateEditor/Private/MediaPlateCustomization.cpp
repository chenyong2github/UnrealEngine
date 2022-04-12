// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "MediaPlate.h"
#include "MediaPlateComponent.h"
#include "MediaPlateEditorModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "FMediaPlateCustomization"

/* IDetailCustomization interface
 *****************************************************************************/

void FMediaPlateCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// Is this the media plate editor window?
	const IDetailsView* DetailsView = DetailBuilder.GetDetailsView();
	TSharedPtr<FTabManager> HostTabManager = DetailsView->GetHostTabManager();
	bool bIsMediaPlateWindow = (HostTabManager.IsValid() == false);

	IDetailCategoryBuilder& MediaPlateCategory = DetailBuilder.EditCategory("MediaPlate");

	// Get objects we are editing.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	MediaPlatesList.Reserve(Objects.Num());
	for (TWeakObjectPtr<UObject>& Obj : Objects)
	{
		TWeakObjectPtr<UMediaPlateComponent> MediaPlate = Cast<UMediaPlateComponent>(Obj.Get());
		if (MediaPlate.IsValid())
		{
			MediaPlatesList.Add(MediaPlate);
		}
	}

	// Get media source property.
	MediaSourceProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMediaPlateComponent, MediaSource));
	
	// Get media path property.
	TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMediaPlateComponent, MediaPath));
	if (Property->IsValidHandle())
	{
		MediaPathProperty = Property->GetChildHandle("FilePath");
		if (MediaPathProperty->IsValidHandle())
		{
			if (MediaSourceProperty->IsValidHandle())
			{
				// Get a callback when this changes so we can hide the media source property.
				FSimpleDelegate OnUrlChangedDelegate = FSimpleDelegate::CreateSP(this,
					&FMediaPlateCustomization::OnMediaPathChanged, &DetailBuilder);
				MediaPathProperty->SetOnPropertyValueChanged(OnUrlChangedDelegate);

				// Is the media path being used?
				FString MediaPath;
				if (MediaPathProperty->GetValue(MediaPath))
				{
					if (MediaPath.IsEmpty() == false)
					{
						// Yes, so hide the media source.
						MediaSourceProperty->MarkHiddenByCustomization();
					}
				}
			}
		}
	}

	// Add media control buttons.
	MediaPlateCategory.AddCustomRow(LOCTEXT("MediaPlateControls", "MediaPlate Controls"))
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
							for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
							{
								UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
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
							for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
							{
								UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
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
	if (bIsMediaPlateWindow == false)
	{
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
	}

FReply FMediaPlateCustomization::OnOpenMediaPlate()
{
	// Get all our objects.
	TArray<UObject*> AssetArray;
	for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
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

void FMediaPlateCustomization::OnMediaPathChanged(IDetailLayoutBuilder* DetailBuilder)
{
	// Refresh the layout so we can show/hide the media source.
	DetailBuilder->ForceRefreshDetails();
}

#undef LOCTEXT_NAMESPACE
