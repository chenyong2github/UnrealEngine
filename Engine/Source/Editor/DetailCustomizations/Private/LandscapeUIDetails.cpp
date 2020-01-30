// Copyright Epic Games, Inc. All Rights Reserved.

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
		TWeakObjectPtr<ALandscape> Landscape(Cast<ALandscape>(EditingObjects[0].Get()));
		if (!Landscape.IsValid())
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
		const FText DisplayAndFilterText(LOCTEXT("LandscapeToggleLayerName", "Enable Edit Layers"));
		const FText ToolTipText(LOCTEXT("LandscapeToggleLayerToolTip", "Toggle whether or not to support edit layers on this Landscape. Toggling this will clear the undo stack."));
		DetailBuilder.AddCustomRowToCategory(PropertyHandle, DisplayAndFilterText)
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget(DisplayAndFilterText, ToolTipText)
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.ToolTipText(ToolTipText)
			.Type(ESlateCheckBoxType::CheckBox)
			.IsChecked_Lambda([=]()
			{
				return Landscape.IsValid() && Landscape->CanHaveLayersContent() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([=](ECheckBoxState NewState)
			{
				bool bChecked = (NewState == ECheckBoxState::Checked);
				if (Landscape.IsValid() && (Landscape->CanHaveLayersContent() != bChecked))
				{
					ToggleCanHaveLayersContent(Landscape.Get());
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
			Reason = LOCTEXT("LandscapeDisableLayers_HiddenLayers", "Are you sure you want to disable the edit layers on this Landscape?\n\nDoing so, will result in losing the data stored for each edit layer, but the current visual output will be kept. Be aware that some edit layers are currently hidden, continuing will result in their data being lost. Undo/redo buffer will also be cleared.");
		}
		else
		{
			Reason = LOCTEXT("LandscapeDisableLayers", "Are you sure you want to disable the edit layers on this Landscape?\n\nDoing so, will result in losing the data stored for each edit layers, but the current visual output will be kept. Undo/redo buffer will also be cleared.");
		}

		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, Reason) == EAppReturnType::Yes;
	}
	else
	{
		
		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("LandscapeEnableLayers", "Are you sure you want to enable edit layers on this landscape? Doing so will clear the undo/redo buffer.")) == EAppReturnType::Yes;
	}

	if (bToggled)
	{
		Landscape->ToggleCanHaveLayersContent();
		if (GEditor)
		{
			GEditor->ResetTransaction(LOCTEXT("ToggleLanscapeLayers", "Toggling Landscape Edit Layers"));
		}
	}
}

#undef LOCTEXT_NAMESPACE
