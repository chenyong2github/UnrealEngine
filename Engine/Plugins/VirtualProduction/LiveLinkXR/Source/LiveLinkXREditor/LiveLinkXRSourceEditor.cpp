// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkXRSourceEditor.h"
#include "LiveLinkXR/LiveLinkXR.h"

#include "DetailLayoutBuilder.h"
#include "LiveLinkXR/LiveLinkXRSourceSettings.h"

#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "LiveLinkXRSourceEditor"

void SLiveLinkXRSourceEditor::Construct(const FArguments& Args)
{
	OnSourceSettingAccepted = Args._OnSourceSettingAccepted;

	FDetailsViewArgs DetailArgs;

	FPropertyEditorModule& PropertyEditor = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedRef<IDetailsView> DetailsView = PropertyEditor.CreateDetailView(DetailArgs);
	DetailsView->HideFilterArea(true);
	DetailsView->SetObject(GetMutableDefault<ULiveLinkXRSettingsObject>(), true);

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			DetailsView
		]
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Right)
		.AutoHeight()
		[
			SNew(SButton)
			.OnClicked(this, &SLiveLinkXRSourceEditor::OnSettingAccepted)
			.Text(LOCTEXT("AddSource", "Add"))
		]
	];
}

FReply SLiveLinkXRSourceEditor::OnSettingAccepted()
{
	OnSourceSettingAccepted.ExecuteIfBound(GetDefault<ULiveLinkXRSettingsObject>()->Settings);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE