// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LandscapeUIDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"
#include "Runtime/Landscape/Classes/Landscape.h"
#include "Settings/EditorExperimentalSettings.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SCheckBox.h"
#include "Misc/MessageDialog.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "FLandscapeUIDetails"

FLandscapeUIDetails::FLandscapeUIDetails()
{
}

FLandscapeUIDetails::~FLandscapeUIDetails()
{
}

TSharedRef<IDetailCustomization> FLandscapeUIDetails::MakeInstance()
{
	return MakeShareable( new FLandscapeUIDetails);
}

void FLandscapeUIDetails::CustomizeDetails( IDetailLayoutBuilder& DetailBuilder )
{
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);

	if (EditingObjects.Num() == 1)
	{
		ALandscape* Landscape = Cast<ALandscape>(EditingObjects[0].Get());
		if (Landscape == nullptr)
		{
			return;
		}

		if (Landscape->NumSubsections == 1)
		{
			TSharedRef<IPropertyHandle> ComponentScreenSizeToUseSubSectionsProp = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ALandscapeProxy, ComponentScreenSizeToUseSubSections));
			DetailBuilder.HideProperty(ComponentScreenSizeToUseSubSectionsProp);
		}
							   
		TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ALandscape, bCanHaveLayersContent));
		DetailBuilder.HideProperty(PropertyHandle);
		const FText DisplayAndFilterText(LOCTEXT("LandscapeToggleLayerName", "Enable Layer System"));
		DetailBuilder.AddCustomRowToCategory(PropertyHandle, DisplayAndFilterText)
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget(DisplayAndFilterText)
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("LandscapeToggleLayerToolTip", "Toggle whether or not to support layers on this Landscape and its streaming proxies. Toggling this will clear the undo stack."))
			.Type(ESlateCheckBoxType::CheckBox)
			.IsChecked_Lambda([=]()
			{
				return Landscape->CanHaveLayersContent() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([=](ECheckBoxState NewState)
			{
				bool bChecked = (NewState == ECheckBoxState::Checked);
				if (Landscape->CanHaveLayersContent() != bChecked)
				{
					ToggleCanHaveLayersContent(Landscape);
				}
			})
		];
	}
}

void FLandscapeUIDetails::ToggleCanHaveLayersContent(ALandscape* Landscape)
{
	bool bToggled = false;

	if (Landscape->bCanHaveLayersContent)
	{
		bool bHasHiddenLayers = false;
		for (int32 i = 0; i < Landscape->GetLayerCount(); ++i)
		{
			const FLandscapeLayer* Layer = Landscape->GetLayer(i);
			check(Layer != nullptr);

			if (!Layer->bVisible)
			{
				bHasHiddenLayers = true;
				break;
			}
		}
				
		FText Reason;

		if (bHasHiddenLayers)
		{
			Reason = LOCTEXT("LandscapeDisableLayers_HiddenLayers", "Are you sure you want to disable the layers system?\n\nDoing so, will result in losing the data stored for each layers, but the current visual output will be kept. Be aware that some layers are currently hidden, continuing will result in their data being lost. Undo/redo buffer will also be cleared.");
		}
		else
		{
			Reason = LOCTEXT("LandscapeDisableLayers", "Are you sure you want to disable the layers system?\n\nDoing so, will result in losing the data stored for each layers, but the current visual output will be kept. Undo/redo buffer will also be cleared.");
		}

		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, Reason) == EAppReturnType::Yes;
	}
	else
	{
		
		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("LandscapeEnableLayers", "Are you sure you want to enable the layer system on this landscape and streaming proxies? Doing so will clear the undo/redo buffer.")) == EAppReturnType::Yes;
	}

	if (bToggled)
	{
		Landscape->ToggleCanHaveLayersContent();
		if (GEditor)
		{
			GEditor->ResetTransaction(LOCTEXT("ToggleLanscapeLayers", "Toggling Landscape Layers"));
		}
	}
}

#undef LOCTEXT_NAMESPACE