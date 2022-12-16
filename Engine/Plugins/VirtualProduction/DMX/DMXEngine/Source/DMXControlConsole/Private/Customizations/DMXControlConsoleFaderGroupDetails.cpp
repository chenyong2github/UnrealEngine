// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFaderGroupDetails.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Layout/Visibility.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleFaderGroupDetails"

void FDMXControlConsoleFaderGroupDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	const TObjectPtr<UDMXControlConsole> ControlConsole = FDMXControlConsoleManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	PropertyUtilities = InDetailLayout.GetPropertyUtilities();

	IDetailCategoryBuilder& FaderGroupCategory = InDetailLayout.EditCategory("DMX Fader Group", FText::GetEmpty());

	FixturePatchRefHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetFixturePatchRefPropertyName());
	InDetailLayout.HideProperty(FixturePatchRefHandle);

	const TSharedPtr<IPropertyHandle> FaderGroupNameHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetFaderGroupNamePropertyName());
	InDetailLayout.HideProperty(FaderGroupNameHandle);
	const TSharedPtr<IPropertyHandle> EditorColorHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetEditorColorPropertyName());
	InDetailLayout.HideProperty(EditorColorHandle);
	const TSharedPtr<IPropertyHandle> FadersHandle = InDetailLayout.GetProperty(UDMXControlConsoleFaderGroup::GetFadersPropertyName());
	InDetailLayout.HideProperty(FadersHandle);

	FaderGroupCategory.AddProperty(FaderGroupNameHandle);
	FaderGroupCategory.AddProperty(EditorColorHandle)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &FDMXControlConsoleFaderGroupDetails::GetEditorColorVisibility));

	FaderGroupCategory.AddProperty(FixturePatchRefHandle);

	FaderGroupCategory.AddCustomRow(FText::GetEmpty())
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &FDMXControlConsoleFaderGroupDetails::GetClearButtonVisibility))
		.WholeRowContent()
		[
			SNew(SBox)
			.Padding(5.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.OnClicked(this, &FDMXControlConsoleFaderGroupDetails::OnClearButtonClicked)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(LOCTEXT("Clear", "Clear"))
				]
			]
		];
}

void FDMXControlConsoleFaderGroupDetails::ForceRefresh() const
{
	if (!PropertyUtilities.IsValid())
	{
		return;
	}
	
	PropertyUtilities->ForceRefresh();
}

bool FDMXControlConsoleFaderGroupDetails::DoSelectedFaderGroupsHaveAnyFixturePatches() const
{
	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();

	bool bHasFixturePatch = false;
	if (SelectedFaderGroupsObjects.IsEmpty())
	{
		return bHasFixturePatch;
	}

	for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
	{
		const UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
		if (!SelectedFaderGroup)
		{
			continue;
		}

		const FDMXEntityFixturePatchRef FixturePatchRef = SelectedFaderGroup->GetFixturePatchRef();
		if (!FixturePatchRef.GetFixturePatch())
		{
			continue;
		}

		bHasFixturePatch = true;
		break;
	}

	return bHasFixturePatch;
}

FReply FDMXControlConsoleFaderGroupDetails::OnClearButtonClicked()
{
	if (!FixturePatchRefHandle.IsValid())
	{
		return FReply::Unhandled();
	}

	FixturePatchRefHandle->NotifyPostChange(EPropertyChangeType::Interactive);
	ForceRefresh();

	return FReply::Handled();
}

EVisibility FDMXControlConsoleFaderGroupDetails::GetEditorColorVisibility() const
{
	return DoSelectedFaderGroupsHaveAnyFixturePatches() ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility FDMXControlConsoleFaderGroupDetails::GetClearButtonVisibility() const
{
	return DoSelectedFaderGroupsHaveAnyFixturePatches() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
