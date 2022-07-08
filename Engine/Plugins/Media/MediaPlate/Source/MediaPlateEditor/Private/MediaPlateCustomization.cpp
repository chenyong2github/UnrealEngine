// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaPlateCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailGroup.h"
#include "LevelEditorViewport.h"
#include "MediaPlate.h"
#include "MediaPlateComponent.h"
#include "MediaPlateEditorModule.h"
#include "MediaPlayer.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
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
		Style = &FAppStyle::Get();
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
			MeshMode = MediaPlate->VisibleMipsTilesCalculations;
		}
	}

	// Set media path.
	UpdateMediaPath();

	// Add mesh customization.
	AddMeshCustomization(MediaPlateCategory);
	
	// Create playlist group.
	IDetailGroup& PlaylistGroup = MediaPlateCategory.AddGroup(TEXT("Playlist"),
		LOCTEXT("Playlist", "Playlist"));
	TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(
		GET_MEMBER_NAME_CHECKED(UMediaPlateComponent, MediaPlaylist));
	PlaylistGroup.HeaderProperty(PropertyHandle);
	PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this,
		&FMediaPlateCustomization::OnPlaylistChanged));

	// Add media source.
	PlaylistGroup.AddWidgetRow()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MediaSource", "Media Source"))
			.ToolTipText(LOCTEXT("MediaSource_ToolTip", "The Media Source to play."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SObjectPropertyEntryBox)
				.AllowedClass(UMediaSource::StaticClass())
				.ObjectPath(this, &FMediaPlateCustomization::GetMediaSourcePath)
				.OnObjectChanged(this, &FMediaPlateCustomization::OnMediaSourceChanged)
		];

	// Add media path.
	FString FileTypeFilter = TEXT("All files (*.*)|*.*");
	PlaylistGroup.AddWidgetRow()
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("MediaPath", "Media Path"))
				.ToolTipText(LOCTEXT("MediaPath_ToolTip",
					"The path of the Media Source to play.\nChanging this will create a new media source in the level to play this path."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SFilePathPicker)
				.BrowseButtonImage(FAppStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
				.BrowseButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.BrowseButtonToolTip(LOCTEXT("FileButtonToolTipText", "Choose a file from this computer"))
				.BrowseTitle(LOCTEXT("PropertyEditorTitle", "File picker..."))
				.FilePath(this, &FMediaPlateCustomization::HandleMediaPath)
				.FileTypeFilter(FileTypeFilter)
				.OnPathPicked(this, &FMediaPlateCustomization::HandleMediaPathPicked)
		];

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

void FMediaPlateCustomization::AddMeshCustomization(IDetailCategoryBuilder& MediaPlateCategory)
{
	// Create detail group.
	IDetailGroup& DetailGroup = MediaPlateCategory.AddGroup(TEXT("Mesh"),
		LOCTEXT("Mesh", "Mesh"));

	// Add radio buttons for mesh type.
	DetailGroup.AddWidgetRow()
		[
			SNew(SSegmentedControl<EMediaTextureVisibleMipsTiles>)
				.Value_Lambda([this]()
				{
					return MeshMode;
				})
				.OnValueChanged(this, &FMediaPlateCustomization::SetMeshMode)

			+ SSegmentedControl<EMediaTextureVisibleMipsTiles>::Slot(EMediaTextureVisibleMipsTiles::Plane)
				.Text(LOCTEXT("Plane", "Plane"))
				.ToolTip(LOCTEXT("Plane_ToolTip",
					"Select this if you want to use a standard plane for the mesh."))

			+ SSegmentedControl<EMediaTextureVisibleMipsTiles>::Slot(EMediaTextureVisibleMipsTiles::Sphere)
				.Text(LOCTEXT("Sphere", "Sphere"))
				.ToolTip(LOCTEXT("Sphere_ToolTip",
					"Select this if you want to use a spherical object for the mesh."))

			+ SSegmentedControl<EMediaTextureVisibleMipsTiles>::Slot(EMediaTextureVisibleMipsTiles::None)
				.Text(LOCTEXT("Custom", "Custom"))
				.ToolTip(LOCTEXT("Custom_ToolTip",
					"Select this if you want to provide your own mesh."))
		];

	// Visibility attributes.
	TAttribute<EVisibility> MeshPlaneVisibility(this, &FMediaPlateCustomization::ShouldShowMeshPlaneWidgets);
	TAttribute<EVisibility> MeshSphereVisibility(this, &FMediaPlateCustomization::ShouldShowMeshSphereWidgets);

	// Add aspect ratio.
	DetailGroup.AddWidgetRow()
		.Visibility(MeshPlaneVisibility)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("AspectRatio", "Aspect Ratio"))
				.ToolTipText(LOCTEXT("AspectRatio_ToolTip",
				"Sets the aspect ratio of the plane showing the media.\nChanging this will change the scale of the mesh component."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)

			// Presets button.
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SComboButton)
						.OnGetMenuContent(this, &FMediaPlateCustomization::OnGetAspectRatios)
						.ContentPadding(2)
						.ButtonContent()
						[
							SNew(STextBlock)
								.ToolTipText(LOCTEXT("Presets_ToolTip", "Select one of the presets for the aspect ratio."))
								.Text(LOCTEXT("Presets", "Presets"))
						]
				]

			// Numeric entry box.
			+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SNumericEntryBox<float>)
						.Value(this, &FMediaPlateCustomization::GetAspectRatio)
						.MinValue(0.0f)
						.OnValueChanged(this, &FMediaPlateCustomization::SetAspectRatio)
				]
		];

	// Add auto aspect ratio.
	DetailGroup.AddWidgetRow()
		.Visibility(MeshPlaneVisibility)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("AutoAspectRatio", "Auto Aspect Ratio"))
				.ToolTipText(LOCTEXT("AutoAspectRatio_ToolTip",
					"Sets the aspect ratio to match the media."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
				.IsChecked(this, &FMediaPlateCustomization::IsAspectRatioAuto)
				.OnCheckStateChanged(this, &FMediaPlateCustomization::SetIsAspectRatioAuto)
		];

	// Add sphere horizontal arc.
	DetailGroup.AddWidgetRow()
		.Visibility(MeshSphereVisibility)
		.NameContent()
		[
			SNew(STextBlock)
				.Text(LOCTEXT("HorizontalArc", "Horizontal Arc"))
				.ToolTipText(LOCTEXT("HorizontalArc_ToolTip",
				"Sets the horizontal arc size of the sphere in degrees.\nFor example 360 for a full circle, 180 for a half circle."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SNumericEntryBox<float>)
				.Value(this, &FMediaPlateCustomization::GetMeshHorizontalRange)
				.OnValueChanged(this, &FMediaPlateCustomization::SetMeshHorizontalRange)
		];
	
}

EVisibility FMediaPlateCustomization::ShouldShowMeshPlaneWidgets() const
{
	return (MeshMode == EMediaTextureVisibleMipsTiles::Plane) ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility FMediaPlateCustomization::ShouldShowMeshSphereWidgets() const
{
	return (MeshMode == EMediaTextureVisibleMipsTiles::Sphere) ? EVisibility::Visible : EVisibility::Hidden;
}

void FMediaPlateCustomization::SetMeshMode(EMediaTextureVisibleMipsTiles InMode)
{
	if (MeshMode != InMode)
	{
		MeshMode = InMode;
		for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
		{
			UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
			if (MediaPlate != nullptr)
			{
				// Update the setting in the media plate.
				MediaPlate->VisibleMipsTilesCalculations = MeshMode;
				MediaPlate->OnVisibleMipsTilesCalculationsChange();

				// Set the appropriate mesh.
				if (MeshMode == EMediaTextureVisibleMipsTiles::Plane)
				{
					MeshCustomization.SetPlaneMesh(MediaPlate);
				}
				else if (MeshMode == EMediaTextureVisibleMipsTiles::Sphere)
				{
					SetSphereMesh(MediaPlate);
				}
			}
		}
	}
}

void FMediaPlateCustomization::SetSphereMesh(UMediaPlateComponent* MediaPlate)
{
	MeshCustomization.SetSphereMesh(MediaPlate);
}

ECheckBoxState FMediaPlateCustomization::IsAspectRatioAuto() const
{
	ECheckBoxState State = ECheckBoxState::Undetermined;

	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			ECheckBoxState NewState = MediaPlate->bIsAspectRatioAuto ? ECheckBoxState::Checked :
				ECheckBoxState::Unchecked;
			if (State == ECheckBoxState::Undetermined)
			{
				State = NewState;
			}
			else if (State != NewState)
			{
				// If the media plates have different states then return undetermined.
				State = ECheckBoxState::Undetermined;
				break;
			}
		}
	}

	return State;
}

void FMediaPlateCustomization::SetIsAspectRatioAuto(ECheckBoxState State)
{
	bool bEnable = (State == ECheckBoxState::Checked);

	// Loop through all our objects.
	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			MediaPlate->bIsAspectRatioAuto = bEnable;
		}
	}
}

TSharedRef<SWidget> FMediaPlateCustomization::OnGetAspectRatios()
{
	FMenuBuilder MenuBuilder(true, NULL);

	FUIAction Set16x9Action(FExecuteAction::CreateSP(this, &FMediaPlateCustomization::SetAspectRatio, 16.0f / 9.0f));
	MenuBuilder.AddMenuEntry(LOCTEXT("16x9", "16x9"), FText(), FSlateIcon(), Set16x9Action);

	FUIAction Set16x10Action(FExecuteAction::CreateSP(this, &FMediaPlateCustomization::SetAspectRatio, 16.0f / 10.0f));
	MenuBuilder.AddMenuEntry(LOCTEXT("16x10", "16x10"), FText(), FSlateIcon(), Set16x10Action);

	FUIAction Set4x3Action(FExecuteAction::CreateSP(this, &FMediaPlateCustomization::SetAspectRatio, 4.0f / 3.0f));
	MenuBuilder.AddMenuEntry(LOCTEXT("4x3", "4x3"), FText(), FSlateIcon(), Set4x3Action);

	FUIAction Set1x1Action(FExecuteAction::CreateSP(this, &FMediaPlateCustomization::SetAspectRatio, 1.0f));
	MenuBuilder.AddMenuEntry(LOCTEXT("1x1", "1x1"), FText(), FSlateIcon(), Set1x1Action);

	return MenuBuilder.MakeWidget();
}

void FMediaPlateCustomization::SetAspectRatio(float AspectRatio)
{
	// Loop through all our objects.
	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			MediaPlate->SetAspectRatio(AspectRatio);
		}
	}

	// Invalidate the viewport so we can see the mesh change.
	if (GCurrentLevelEditingViewportClient != nullptr)
	{
		GCurrentLevelEditingViewportClient->Invalidate();
	}
}


TOptional<float> FMediaPlateCustomization::GetAspectRatio() const
{
	// Loop through our objects.
	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			AActor* Owner = MediaPlate->GetOwner();
			AMediaPlate* MediaPlateActor = Cast<AMediaPlate>(Owner);
			if (MediaPlateActor != nullptr)
			{
				// Get the static mesh.
				UStaticMeshComponent* StaticMeshComponent = MediaPlateActor->StaticMeshComponent;
				if (StaticMeshComponent != nullptr)
				{
					// Calculate aspect ratio from the scale.
					FVector Scale = StaticMeshComponent->GetComponentScale();
					float AspectRatio = 0.0f;
					if (Scale.Z != 0.0f)
					{
						AspectRatio = Scale.Y / Scale.Z;
					}
					return AspectRatio;
				}
			}
			break;
		}
	}

	return TOptional<float>();
}

void FMediaPlateCustomization::SetMeshHorizontalRange(float HorizontalRange)
{
	HorizontalRange = FMath::Clamp(HorizontalRange, 0.0f, 360.0f);

	// Loop through all our objects.
	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			if (MediaPlate->GetMeshHorizontalRange() != HorizontalRange)
			{
				MediaPlate->SetMeshHorizontalRange(HorizontalRange);
				SetSphereMesh(MediaPlate);
			}
		}
	}
}

TOptional<float> FMediaPlateCustomization::GetMeshHorizontalRange() const
{
	// Loop through our objects.
	for (const TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			return MediaPlate->GetMeshHorizontalRange();
		}
	}

	return TOptional<float>();
}

FString FMediaPlateCustomization::GetMediaSourcePath() const
{
	FString Path;

	// Get the first media plate.
	if (MediaPlatesList.Num() > 0)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatesList[0].Get();
		if (MediaPlate != nullptr)
		{
			UMediaPlaylist* Playlist = MediaPlate->MediaPlaylist;
			if (Playlist != nullptr)
			{
				// Get the first media source in the playlist.
				UMediaSource* MediaSource = Playlist->Get(0);
				if (MediaSource != nullptr)
				{
					Path = MediaSource->GetPathName();
				}
			}
		}
	}

	return Path;
}

void FMediaPlateCustomization::OnPlaylistChanged()
{
	StopMediaPlates();
	UpdateMediaPath();
}

void FMediaPlateCustomization::OnMediaSourceChanged(const FAssetData& AssetData)
{
	// Update the playlist with the new media source.
	UMediaSource* MediaSource = Cast<UMediaSource>(AssetData.GetAsset());
	for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
		if (MediaPlate != nullptr)
		{
			// Get playlist.
			UMediaPlaylist* Playlist = MediaPlate->MediaPlaylist;
			if (Playlist == nullptr)
			{
				Playlist = NewObject<UMediaPlaylist>(MediaPlate);
				MediaPlate->MediaPlaylist = Playlist;
			}
			
			// Update playlist.
			if (Playlist->Num() > 0)
			{
				Playlist->Replace(0, MediaSource);
			}
			else
			{
				Playlist->Add(MediaSource);
			}
			Playlist->MarkPackageDirty();
		}
	}

	StopMediaPlates();
	UpdateMediaPath();
}

void FMediaPlateCustomization::UpdateMediaPath()
{
	MediaPath.Empty();

	// Get the first media plate.
	if (MediaPlatesList.Num() > 0)
	{
		UMediaPlateComponent* MediaPlate = MediaPlatesList[0].Get();
		if (MediaPlate != nullptr)
		{
			UMediaPlaylist* Playlist = MediaPlate->MediaPlaylist;
			if (Playlist != nullptr)
			{
				// Get the first media source in the playlist.
				UMediaSource* MediaSource = Playlist->Get(0);
				if (MediaSource != nullptr)
				{
					MediaPath = MediaSource->GetUrl();

					// Remove certain types.
					const FString FilePrefix(TEXT("file://"));
					const FString ImgPrefix(TEXT("img://"));
					if (MediaPath.StartsWith(FilePrefix))
					{
						MediaPath = MediaPath.RightChop(FilePrefix.Len());
					}
					else if (MediaPath.StartsWith(ImgPrefix))
					{
						MediaPath = MediaPath.RightChop(ImgPrefix.Len());
					}
				}
			}
		}
	}
}

FString FMediaPlateCustomization::HandleMediaPath() const
{
	return MediaPath;
}

void FMediaPlateCustomization::HandleMediaPathPicked(const FString& PickedPath)
{
	// Did we get something?
	if ((PickedPath.IsEmpty() == false) && (PickedPath != MediaPath))
	{
		// Stop playback.
		StopMediaPlates();

		// Set up media source for our media plates.
		for (TWeakObjectPtr<UMediaPlateComponent>& MediaPlatePtr : MediaPlatesList)
		{
			UMediaPlateComponent* MediaPlate = MediaPlatePtr.Get();
			if (MediaPlate != nullptr)
			{
				// Get playlist.
				UMediaPlaylist* Playlist = MediaPlate->MediaPlaylist;
				if (Playlist == nullptr)
				{
					Playlist = NewObject<UMediaPlaylist>(MediaPlate);
					MediaPlate->MediaPlaylist = Playlist;
				}
				
				// Create media source for this path.
				UMediaSource* MediaSource = UMediaSource::SpawnMediaSourceForString(PickedPath, MediaPlate);
				if (MediaSource != nullptr)
				{
					if (Playlist->Num() > 0)
					{
						Playlist->Replace(0, MediaSource);
					}
					else
					{
						Playlist->Add(MediaSource);
					}
					Playlist->MarkPackageDirty();
				}
			}
		}

		// Update the media path.
		UpdateMediaPath();
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

void FMediaPlateCustomization::StopMediaPlates()
{
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
