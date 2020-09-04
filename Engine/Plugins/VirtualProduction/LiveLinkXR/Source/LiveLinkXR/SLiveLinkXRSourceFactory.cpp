// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLiveLinkXRSourceFactory.h"
#include "LiveLinkXR.h"
#include "LiveLinkXRSourceSettings.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#if WITH_EDITOR
#include "DetailLayoutBuilder.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "SLiveLinkXRSourceFactory"

void SLiveLinkXRSourceFactory::Construct(const FArguments& Args)
{
	OnSourceSettingAccepted = Args._OnSourceSettingAccepted;

#if WITH_EDITOR
	FDetailsViewArgs DetailArgs;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedRef<IDetailsView> DetailsView = PropertyEditor.CreateDetailView(DetailArgs);
	DetailsView->HideFilterArea(true);
	DetailsView->SetObject(GetMutableDefault<ULiveLinkXRSettingsObject>(), true);
#endif //WITH_EDITOR

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.f)
#if WITH_EDITOR
		[
			DetailsView
		]
#endif //WITH_EDITOR
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoHeight()
		[
			SNew(SButton)
			.OnClicked(this, &SLiveLinkXRSourceFactory::OnSettingAccepted)
			.Text(LOCTEXT("AddSource", "Add"))
		]
	];
}

FReply SLiveLinkXRSourceFactory::OnSettingAccepted()
{
	OnSourceSettingAccepted.ExecuteIfBound(GetDefault<ULiveLinkXRSettingsObject>()->Settings);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE