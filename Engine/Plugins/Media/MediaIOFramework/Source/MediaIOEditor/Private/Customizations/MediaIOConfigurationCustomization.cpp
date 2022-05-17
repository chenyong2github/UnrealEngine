// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/MediaIOConfigurationCustomization.h"

#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailChildrenBuilder.h"
#include "IMediaIOCoreDeviceProvider.h"
#include "IMediaIOCoreModule.h"
#include "MediaIOPermutationsSelectorBuilder.h"
#include "ObjectEditorUtils.h"
#include "TimeSynchronizableMediaSource.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SMediaPermutationsSelector.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "MediaIOConfigurationCustomization"

TSharedRef<IPropertyTypeCustomization> FMediaIOConfigurationCustomization::MakeInstance()
{
	return MakeShared<FMediaIOConfigurationCustomization>();
}

TAttribute<FText> FMediaIOConfigurationCustomization::GetContentText()
{
	FMediaIOConfiguration* Value = GetPropertyValueFromPropertyHandle<FMediaIOConfiguration>();
	IMediaIOCoreDeviceProvider* DeviceProviderPtr = IMediaIOCoreModule::Get().GetDeviceProvider(DeviceProviderName);
	if (DeviceProviderPtr)
	{
		return MakeAttributeLambda([=] { return DeviceProviderPtr->ToText(*Value); });
	}
	return FText::GetEmpty();
}

TSharedRef<SWidget> FMediaIOConfigurationCustomization::HandleSourceComboButtonMenuContent()
{
	PermutationSelector.Reset();

	IMediaIOCoreDeviceProvider* DeviceProviderPtr = IMediaIOCoreModule::Get().GetDeviceProvider(DeviceProviderName);
	if (DeviceProviderPtr == nullptr)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoDeviceProviderFound", "No provider found"));
	}

	SelectedConfiguration = *GetPropertyValueFromPropertyHandle<FMediaIOConfiguration>();
	bool bIsInput = SelectedConfiguration.bIsInput;
	if (!SelectedConfiguration.IsValid())
	{
		SelectedConfiguration = DeviceProviderPtr->GetDefaultConfiguration();
		SelectedConfiguration.bIsInput = bIsInput;
	}


	TArray<FMediaIOConfiguration> MediaConfigurations = bIsInput ? DeviceProviderPtr->GetConfigurations(true, false) : DeviceProviderPtr->GetConfigurations(false, true);
	if (MediaConfigurations.Num() == 0)
	{
		return SNew(STextBlock)
			.Text(LOCTEXT("NoConfigurationFound", "No configuration found"));
	}

	auto QuadTypeVisible = [](FName ColumnName, const TArray<FMediaIOConfiguration>& UniquePermutationsForThisColumn)
	{
		if (UniquePermutationsForThisColumn.Num() > 0)
		{
			return UniquePermutationsForThisColumn[0].MediaConnection.TransportType == EMediaIOTransportType::QuadLink;
		}
		return false;
	};

	TSharedRef<SWidget> EnforceWidget = SNullWidget::NullWidget;

	if (bIsInput)
	{
		bool bSupportsAutoDetect = true;
		for (UObject* Object : GetCustomizedObjects())
		{
			if (Object && Object->IsA<UTimeSynchronizableMediaSource>())
			{
				const UTimeSynchronizableMediaSource* MediaSource = CastChecked<UTimeSynchronizableMediaSource>(Object);
				bSupportsAutoDetect &= MediaSource->SupportsFormatAutoDetection();
				CustomizedSources.Add(CastChecked<UTimeSynchronizableMediaSource>(Object));
			}
			else
			{
				bSupportsAutoDetect = false;
			}
		}

		if (bSupportsAutoDetect)
		{
			EnforceWidget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(4.f)
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("MediaPlayerEditor", "EnforceLabel", "Enforce format"))
				]
			+ SHorizontalBox::Slot()
				.Padding(4.f)
				.AutoWidth()
				[
					SNew(SCheckBox)
					.ToolTipText(NSLOCTEXT("MediaPlayerEditor", "EnforceTooltip", "Enforce a specific configuration. (Note that this disables format autodetection)"))
					.IsChecked(this, &FMediaIOConfigurationCustomization::GetEnforceCheckboxState)
					.OnCheckStateChanged(this, &FMediaIOConfigurationCustomization::SetEnforceCheckboxState)
				];
		}		

	}

	auto GetExtensions = [EnforceWidget](TArray<TSharedRef<SWidget>>& OutWidgets)
	{
		if (EnforceWidget != SNullWidget::NullWidget)
		{
			OutWidgets.Add(EnforceWidget);
		}
	};

	using TSelection = SMediaPermutationsSelector<FMediaIOConfiguration, FMediaIOPermutationsSelectorBuilder>;
	TSelection::FArguments Arguments;
	Arguments
		.PermutationsSource(MoveTemp(MediaConfigurations))
		.SelectedPermutation(SelectedConfiguration)
		.OnSelectionChanged(this, &FMediaIOConfigurationCustomization::OnSelectionChanged)
		.OnButtonClicked(this, &FMediaIOConfigurationCustomization::OnButtonClicked)
		.OnGetExtensions_Lambda(GetExtensions)
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_DeviceIdentifier)
		.Label(LOCTEXT("DeviceLabel", "Device"));

	if (DeviceProviderPtr->ShowInputTransportInSelector())
	{
		Arguments
			+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_TransportType)
			.Label(LOCTEXT("SourceTypeLabel", "Source"))
			+TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_QuadType)
			.Label(LOCTEXT("QuadTypeLabel", "Quad"))
			.IsColumnVisible_Lambda(QuadTypeVisible);
	}

	Arguments
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_Resolution)
		.Label(LOCTEXT("ResolutionLabel", "Resolution"))
		.IsColumnVisible_Raw(this, &FMediaIOConfigurationCustomization::ShowAdvancedColumns)
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_Standard)
		.Label(LOCTEXT("StandardLabel", "Standard"))
		.IsColumnVisible_Raw(this, &FMediaIOConfigurationCustomization::ShowAdvancedColumns)
		+ TSelection::Column(FMediaIOPermutationsSelectorBuilder::NAME_FrameRate)
		.Label(LOCTEXT("FrameRateLabel", "Frame Rate"))
		.IsColumnVisible_Raw(this, &FMediaIOConfigurationCustomization::ShowAdvancedColumns);



	TSharedRef<TSelection> Selector = SNew(TSelection) = Arguments;
	PermutationSelector = Selector;
	SelectedConfiguration = Selector->GetSelectedItem();

	return Selector;
}

void FMediaIOConfigurationCustomization::OnSelectionChanged(FMediaIOConfiguration SelectedItem)
{
	SelectedConfiguration = SelectedItem;
}

FReply FMediaIOConfigurationCustomization::OnButtonClicked() const
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

bool FMediaIOConfigurationCustomization::ShowAdvancedColumns(FName ColumnName, const TArray<FMediaIOConfiguration>& UniquePermutationsForThisColumn) const
{
	bool bAutoDetectInput = true;

	for (const TWeakObjectPtr<UTimeSynchronizableMediaSource>& WeakMediaSource : CustomizedSources)
	{
		if (UTimeSynchronizableMediaSource* MediaSource = WeakMediaSource.Get())
		{
			if (!MediaSource->SupportsFormatAutoDetection())
			{
				return true;
			}
			if (!MediaSource->bAutoDetectInput)
			{
				bAutoDetectInput = false;
				break;
			}
		}
	}

	return !bAutoDetectInput;
}

ECheckBoxState FMediaIOConfigurationCustomization::GetEnforceCheckboxState() const
{
	bool bAllAutoDetect = true;

	for (const TWeakObjectPtr<UTimeSynchronizableMediaSource>& WeakMediaSource : CustomizedSources)
	{
		if(UTimeSynchronizableMediaSource * MediaSource = WeakMediaSource.Get(); !MediaSource->bAutoDetectInput)
		{
			bAllAutoDetect = false;
			break;
		}
	}

	return bAllAutoDetect ? ECheckBoxState::Unchecked : ECheckBoxState::Checked;
}

void FMediaIOConfigurationCustomization::SetEnforceCheckboxState(ECheckBoxState CheckboxState)
{
	bool bEnforceFormat = CheckboxState == ECheckBoxState::Checked;

	for (const TWeakObjectPtr<UTimeSynchronizableMediaSource>& WeakMediaSource : CustomizedSources)
	{
		if (UTimeSynchronizableMediaSource* MediaSource = WeakMediaSource.Get())
		{
			MediaSource->bAutoDetectInput = !bEnforceFormat;
		}
	}
}

#undef LOCTEXT_NAMESPACE
