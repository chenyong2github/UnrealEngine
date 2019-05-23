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
#include "Widgets/Text/STextBlock.h"
#include "Misc/MessageDialog.h"
#include "Editor.h"

FLandscapeUIDetails::FLandscapeUIDetails()
{
}

FLandscapeUIDetails::~FLandscapeUIDetails()
{
	if (DetailLayoutBuilder)
	{
		GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().RemoveAll(this);
	}
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

		if (Landscape != nullptr && Landscape->NumSubsections == 1)
		{
			TSharedRef<IPropertyHandle> ComponentScreenSizeToUseSubSectionsProp = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ALandscapeProxy, ComponentScreenSizeToUseSubSections));
			DetailBuilder.HideProperty(ComponentScreenSizeToUseSubSectionsProp);
		}

		if (Landscape != nullptr)
		{
			if (!DetailLayoutBuilder)
			{
				DetailLayoutBuilder = &DetailBuilder;
				GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().AddSP(this, &FLandscapeUIDetails::OnEditorExperimentalSettingsChanged);
			}

			TSharedRef<IPropertyHandle> PropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ALandscape, bCanHaveLayersContent));
			DetailBuilder.HideProperty(PropertyHandle);
			if(GetMutableDefault<UEditorExperimentalSettings>()->bLandscapeLayerSystem)
			{
				IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(PropertyHandle->GetDefaultCategoryName());
				Category.AddCustomRow(FText(), true)
				.NameContent()
				[
					PropertyHandle->CreatePropertyNameWidget(FText::FromString("Layer System"))
				]
				.ValueContent()
				[
					SNew(SButton)
					.OnClicked_Lambda([this, Landscape]() -> FReply
					{
						return ToggleCanHaveLayersContent(Landscape);
					})
					[
						SNew(STextBlock)
						.Text_Lambda([Landscape]()
						{
							return Landscape->bCanHaveLayersContent ? NSLOCTEXT("UnrealEd", "LandscapeDisableLayers", "Disable") : NSLOCTEXT("UnrealEd", "LandscapeEnableLayers", "Enable");	
						})
					]
				];
			}
			
		}
	}
}

void FLandscapeUIDetails::OnEditorExperimentalSettingsChanged(FName PropertyName)
{
	if (DetailLayoutBuilder && PropertyName == GET_MEMBER_NAME_CHECKED(UEditorExperimentalSettings, bLandscapeLayerSystem))
	{
		DetailLayoutBuilder->ForceRefreshDetails();
	}
}

FReply FLandscapeUIDetails::ToggleCanHaveLayersContent(ALandscape* Landscape)
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
			Reason = NSLOCTEXT("UnrealEd", "LandscapeDisableLayers_HiddenLayers", "Are you sure you want to disable the layers system?\n\nDoing so, will result in losing the data stored for each layers, but the current visual output will be kept. Be aware that some layers are currently hidden, continuing will result in their data being lost. Undo/redo buffer will also be cleared.");
		}
		else
		{
			Reason = NSLOCTEXT("UnrealEd", "LandscapeDisableLayers", "Are you sure you want to disable the layers system?\n\nDoing so, will result in losing the data stored for each layers, but the current visual output will be kept. Undo/redo buffer will also be cleared.");
		}

		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, Reason) == EAppReturnType::Yes;
	}
	else
	{
		
		bToggled = FMessageDialog::Open(EAppMsgType::YesNo, NSLOCTEXT("UnrealEd", "LandscapeEnableLayers", "Are you sure you want to enable the layer system on this landscape and streaming proxies? Doing so will clear the undo/redo buffer.")) == EAppReturnType::Yes;
	}

	if (DetailLayoutBuilder && bToggled)
	{
		Landscape->ToggleCanHaveLayersContent();
		GEditor->ResetTransaction(NSLOCTEXT("UnrealEd", "ToggleLanscapeLayers", "Toggling Landscape Layers"));
		DetailLayoutBuilder->ForceRefreshDetails();
	}

	return FReply::Handled();
}