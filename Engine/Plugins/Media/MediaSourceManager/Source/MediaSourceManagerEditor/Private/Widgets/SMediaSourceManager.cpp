// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManager.h"

#include "MediaSourceManager.h"
#include "MediaSourceManagerEditorModule.h"
#include "MediaSourceManagerSettings.h"
#include "MediaTexture.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SMediaPlayerEditorViewer.h"
#include "Widgets/SMediaSourceManagerSources.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManager"

void SMediaSourceManager::Construct(const FArguments& InArgs)
{
	MediaSourceManagerPtr = GetDefault<UMediaSourceManagerSettings>()->GetManager();
	
	ChildSlot
		[
			SNew(SVerticalBox)

			// Manager selector.
			+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(2)
				.VAlign(VAlign_Center)
				[
					SAssignNew(ManagerButton, SComboButton)
						.OnGetMenuContent(this, &SMediaSourceManager::GetManagerPicker)
						.ButtonContent()
						[
							SNew(STextBlock)
								.Text(this, &SMediaSourceManager::GetManagerName)
								.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						]
				]

			// Sources.
			+ SVerticalBox::Slot()
				.Padding(2)
				.VAlign(VAlign_Center)
				[
					SAssignNew(SourcesWidget, SMediaSourceManagerSources, MediaSourceManagerPtr.Get())
				]
		];
}

FText SMediaSourceManager::GetManagerName() const
{
	UMediaSourceManager* MediaSourceManager = MediaSourceManagerPtr.Get();

	if (MediaSourceManager != nullptr)
	{
		return FText::FromName(MediaSourceManager->GetFName());
	}
	return LOCTEXT("SelectManager", "Select a MediaSourceManager asset.");
}

TSharedRef<SWidget> SMediaSourceManager::GetManagerPicker()
{
	UMediaSourceManager* MediaSourceManager = MediaSourceManagerPtr.Get();
	FAssetData CurrentAssetData = MediaSourceManager ? FAssetData(MediaSourceManager) : FAssetData();

	TArray<const UClass*> ClassFilters;
	ClassFilters.Add(UMediaSourceManager::StaticClass());

	return PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
		CurrentAssetData,
		MediaSourceManagerPtr != nullptr,
		false,
		ClassFilters,
		TArray<UFactory*>(),
		FOnShouldFilterAsset::CreateLambda([CurrentAssetData](const FAssetData& InAssetData) { return InAssetData == CurrentAssetData; }),
		FOnAssetSelected::CreateRaw(this, &SMediaSourceManager::NewManagerSelected),
		FSimpleDelegate()
	);
}

void SMediaSourceManager::NewManagerSelected(const FAssetData& AssetData)
{
	// Close the combo box.
	ManagerButton->SetIsOpen(false);

	// Set new manager.
	UMediaSourceManager* Manager = Cast<UMediaSourceManager>(AssetData.GetAsset());
	MediaSourceManagerPtr = Manager;
	GetMutableDefault<UMediaSourceManagerSettings>()->SetManager(Manager);

	// Refresh sources widget.
	if (SourcesWidget != nullptr)
	{
		SourcesWidget->SetManager(Manager);
	}
}

#undef LOCTEXT_NAMESPACE
