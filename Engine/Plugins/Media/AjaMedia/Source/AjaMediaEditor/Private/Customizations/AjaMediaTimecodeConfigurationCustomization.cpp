// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/AjaMediaTimecodeConfigurationCustomization.h"

#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "MediaIOPermutationsSelectorBuilder.h"
#include "ObjectEditorUtils.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SMediaPermutationsSelector.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "AjaMediaTimecodeConfigurationCustomization"

namespace AjaMediaTimecodeConfigurationCustomization
{
	static const FName NAME_TimecodeFormat("TimecodeFormat");

	struct FMediaTimecodePermutationsSelectorBuilder
	{
		static bool IdenticalProperty(FName ColumnName, const FAjaMediaTimecodeConfiguration& Left, const FAjaMediaTimecodeConfiguration& Right)
		{
			if (ColumnName == NAME_TimecodeFormat) return Left.TimecodeFormat == Right.TimecodeFormat;
			return FMediaIOPermutationsSelectorBuilder::IdenticalProperty(ColumnName, Left.MediaConfiguration, Right.MediaConfiguration);
		}

		static bool Less(FName ColumnName, const FAjaMediaTimecodeConfiguration& Left, const FAjaMediaTimecodeConfiguration& Right)
		{
			if (ColumnName == NAME_TimecodeFormat) return (int32)Left.TimecodeFormat < (int32)Right.TimecodeFormat;
			return FMediaIOPermutationsSelectorBuilder::Less(ColumnName, Left.MediaConfiguration, Right.MediaConfiguration);
		}

		static FText GetLabel(FName ColumnName, const FAjaMediaTimecodeConfiguration& Item)
		{
			if (ColumnName == NAME_TimecodeFormat)
			{
				switch(Item.TimecodeFormat)
				{
				case EMediaIOTimecodeFormat::LTC:
					return LOCTEXT("LtcLabel", "LTC");
				case EMediaIOTimecodeFormat::VITC:
					return LOCTEXT("VITCLabel", "VITC");
				}
				return LOCTEXT("Invalid", "<Invalid>");
			}
			return FMediaIOPermutationsSelectorBuilder::GetLabel(ColumnName, Item.MediaConfiguration);
		}

		static FText GetTooltip(FName ColumnName, const FAjaMediaTimecodeConfiguration& Item)
		{
			if (ColumnName == NAME_TimecodeFormat) return LOCTEXT("ReferenceFrameRateTooltip", "Timecode format to read from a video signal.");
			return FMediaIOPermutationsSelectorBuilder::GetTooltip(ColumnName, Item.MediaConfiguration);
		}
	};
}

TAttribute<FText> FAjaMediaTimecodeConfigurationCustomization::GetContentText()
{
	FAjaMediaTimecodeConfiguration* Value = GetPropertyValueFromPropertyHandle<FAjaMediaTimecodeConfiguration>();
	return MakeAttributeLambda([=] { return Value->ToText(); });
}

TSharedRef<SWidget> FAjaMediaTimecodeConfigurationCustomization::HandleSourceComboButtonMenuContent()
{
	PermutationSelector.Reset();

	FAjaDeviceProvider DeviceProvider;
	SelectedConfiguration = *GetPropertyValueFromPropertyHandle<FAjaMediaTimecodeConfiguration>();
	if (!SelectedConfiguration.IsValid())
	{
		SelectedConfiguration = FAjaMediaTimecodeConfiguration::GetDefault();
	}


	TArray<FAjaMediaTimecodeConfiguration> MediaConfigurations = FAjaDeviceProvider().GetTimecodeConfiguration();

	if (MediaConfigurations.Num() == 0)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoConfigurationFound", "No configuration found"));
	}

	auto QuadTypeVisible = [](FName ColumnName, const TArray<FAjaMediaTimecodeConfiguration>& UniquePermutationsForThisColumn)
	{
		if (UniquePermutationsForThisColumn.Num() > 0)
		{
			return UniquePermutationsForThisColumn[0].MediaConfiguration.MediaConnection.TransportType == EMediaIOTransportType::QuadLink;
		}
		return false;
	};

	using TSelection = SMediaPermutationsSelector<FAjaMediaTimecodeConfiguration, AjaMediaTimecodeConfigurationCustomization::FMediaTimecodePermutationsSelectorBuilder>;
	TSelection::FArguments Arguments;
	Arguments
		.PermutationsSource(MoveTemp(MediaConfigurations))
		.SelectedPermutation(SelectedConfiguration)
		.OnSelectionChanged(this, &FAjaMediaTimecodeConfigurationCustomization::OnSelectionChanged)
		.OnButtonClicked(this, &FAjaMediaTimecodeConfigurationCustomization::OnButtonClicked)
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_DeviceIdentifier)
		.Label(LOCTEXT("DeviceLabel", "Device"))
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_TransportType)
		.Label(LOCTEXT("SourceTypeLabel", "Source"))
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_QuadType)
		.Label(LOCTEXT("QuadTypeLabel", "Quad"))
		.IsColumnVisible_Lambda(QuadTypeVisible)
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_Resolution)
		.Label(LOCTEXT("ResolutionLabel", "Resolution"))
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_Standard)
		.Label(LOCTEXT("StandardLabel", "Standard"))
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_FrameRate)
		.Label(LOCTEXT("FrameRateLabel", "Frame Rate"))
		+ TSelection::Column(AjaMediaTimecodeConfigurationCustomization::NAME_TimecodeFormat)
		.Label(LOCTEXT("TimecodeFormatLabel", "Format"));

	TSharedRef<TSelection> Selector = SNew(TSelection) = Arguments;
	PermutationSelector = Selector;
	SelectedConfiguration = Selector->GetSelectedItem();

	return Selector;
}

void FAjaMediaTimecodeConfigurationCustomization::OnSelectionChanged(FAjaMediaTimecodeConfiguration SelectedItem)
{
	SelectedConfiguration = SelectedItem;
}

FReply FAjaMediaTimecodeConfigurationCustomization::OnButtonClicked() const
{
	AssignValue(SelectedConfiguration);

	TSharedPtr<SWidget> SharedPermutationSelector = PermutationSelector.Pin();
	if (SharedPermutationSelector.IsValid())
	{
		TSharedRef<SWindow> ParentContextMenuWindow = FSlateApplication::Get().FindWidgetWindow(SharedPermutationSelector.ToSharedRef()).ToSharedRef();
		FSlateApplication::Get().RequestDestroyWindow(ParentContextMenuWindow);
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
