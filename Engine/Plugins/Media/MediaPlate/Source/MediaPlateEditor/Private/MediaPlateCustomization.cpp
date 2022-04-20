// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "MediaPlate.h"
#include "MediaPlateComponent.h"
#include "MediaPlateEditorModule.h"
#include "MediaPlayer.h"
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

	// Get style.
	const ISlateStyle* Style = nullptr;
	FMediaPlateEditorModule* EditorModule = FModuleManager::LoadModulePtr<FMediaPlateEditorModule>("MediaPlateEditor");
	if (EditorModule != nullptr)
	{
		Style = EditorModule->GetStyle().Get();
	}
	if (Style == nullptr)
	{
		Style = &FEditorStyle::Get();
	}

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

	// Get the bUseMediaSource property.
	TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMediaPlateComponent, bUseMediaSource));
	if (Property->IsValidHandle())
	{
		
		// Get a callback when this changes so we can stop the player.
		FSimpleDelegate OnUseMediaSourceChangedDelegate = FSimpleDelegate::CreateSP(this,
			&FMediaPlateCustomization::OnUseMediaSourceChanged, &DetailBuilder);
		Property->SetOnPropertyValueChanged(OnUseMediaSourceChangedDelegate);
	}

	// Add media control buttons.
	MediaPlateCategory.AddCustomRow(LOCTEXT("MediaPlateControls", "MediaPlate Controls"))
		[
			SNew(SHorizontalBox)

			// Rewind button.
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
								UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
								if (MediaPlayer != nullptr)
								{
									MediaPlayer->Rewind();
								}
							}
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(Style->GetBrush("MediaPlateEditor.RewindMedia.Small"))
					]
				]

			// Reverse button.
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
									UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
									if (MediaPlayer != nullptr)
									{
										MediaPlayer->SetRate(GetReverseRate(MediaPlayer));
									}
								}
							}
							return FReply::Handled();
						})
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image(Style->GetBrush("MediaPlateEditor.ReverseMedia.Small"))
						]
				]

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
									// Is the player paused or fast forwarding/rewinding?
									UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
									if ((MediaPlayer != nullptr) &&
										((MediaPlayer->IsPaused()) || 
											(MediaPlayer->IsPlaying() && (MediaPlayer->GetRate() != 1.0f))))
									{
										MediaPlayer->Play();
									}
									else
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
							}
							return FReply::Handled();
						})
						[
							SNew(SImage)
								.ColorAndOpacity(FSlateColor::UseForeground())
								.Image(Style->GetBrush("MediaPlateEditor.PlayMedia.Small"))
						]
				]

			// Pause button.
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
								UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
								if (MediaPlayer != nullptr)
								{
									MediaPlayer->Pause();
								}
							}
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image(Style->GetBrush("MediaPlateEditor.PauseMedia.Small"))
					]
				]

			// Forward button.
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
								UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
								if (MediaPlayer != nullptr)
								{
									MediaPlayer->SetRate(GetForwardRate(MediaPlayer));
								}
							}
						}
						return FReply::Handled();
					})
					[
						SNew(SImage)
							.ColorAndOpacity(FSlateColor::UseForeground())
							.Image(Style->GetBrush("MediaPlateEditor.ForwardMedia.Small"))
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
								.Image(Style->GetBrush("MediaPlateEditor.StopMedia.Small"))
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

void FMediaPlateCustomization::OnUseMediaSourceChanged(IDetailLayoutBuilder* DetailBuilder)
{
	// Stop the player if we change the media source.
	for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			MediaPlate->Stop();
		}
	}
}

float FMediaPlateCustomization::GetForwardRate(UMediaPlayer* MediaPlayer) const
{
	float Rate = MediaPlayer->GetRate();

	if (Rate < 1.0f)
	{
		Rate = 1.0f;
	}

	return 2.0f * Rate;
}

float FMediaPlateCustomization::GetReverseRate(UMediaPlayer* MediaPlayer) const
{
	float Rate = MediaPlayer->GetRate();

	if (Rate > -1.0f)
	{
		return -1.0f;
	}

	return 2.0f * Rate;
}


#undef LOCTEXT_NAMESPACE
