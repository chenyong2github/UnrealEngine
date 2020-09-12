// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeModePropertiesDetails.h"

#include "DMXEditor.h"
#include "DMXFixtureTypeSharedData.h"
#include "Library/DMXEntityFixtureType.h"
#include "Widgets/SDMXEntityEditor.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SSeparator.h"


#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeModePropertiesDetails"

void FDMXEntityFixtureTypeModePropertiesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideCategory("Entity Properties");
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXCategory));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bPixelFunctionsEnabled));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		PropertyUtilities = DetailBuilder.GetPropertyUtilities();
		check(PropertyUtilities.IsValid());

		SharedData = DMXEditor->GetFixtureTypeSharedData();
		check(SharedData.IsValid());

		SharedData->OnModesSelected.AddSP(this, &FDMXEntityFixtureTypeModePropertiesDetails::OnModesSelected);

		IDetailCategoryBuilder& ModePropertiesCategory = DetailBuilder.EditCategory("Mode Properties");
				
		TSharedPtr<IPropertyHandle> ModesHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
		check(ModesHandle.IsValid() && ModesHandle->IsValidHandle());
		TSharedPtr<IPropertyHandleArray> ModesHandleArray = ModesHandle->AsArray();
		check(ModesHandleArray.IsValid());

		FSimpleDelegate OnNumModesChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeModePropertiesDetails::OnNumModesChanged);
		ModesHandleArray->SetOnNumElementsChanged(OnNumModesChangedDelegate);

		// Add all mode properties
		uint32 NumModes = 0;
		if (ModesHandle->GetNumChildren(NumModes) == FPropertyAccess::Success)
		{
			for (uint32 IndexOfMode = 0; IndexOfMode < NumModes; IndexOfMode++)
			{
				TSharedRef<IPropertyHandle> ModeHandle = ModesHandleArray->GetElement(IndexOfMode);

				// Hide reset to default buttons for array entries
				ModeHandle->MarkResetToDefaultCustomized();
				
				// Hide reset to default buttons for Mode Members
				uint32 NumChildrenInMode = 0;
				ModeHandle->GetNumChildren(NumChildrenInMode);

				for (uint32 IndexModeChild = 0; IndexModeChild < NumChildrenInMode; IndexModeChild++)
				{
					TSharedPtr<IPropertyHandle> ModeChildHandle  = ModeHandle->GetChildHandle(IndexModeChild);
					check(ModeChildHandle.IsValid() && ModeChildHandle->IsValidHandle());

					ModeChildHandle->MarkResetToDefaultCustomized();
				}

				// Hide reset to default buttons for PixelMatrixConfig
				TSharedPtr<IPropertyHandle> PixelMatrixConfigHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, PixelMatrixConfig));
				check(PixelMatrixConfigHandle.IsValid());

				uint32 NumChildrenInPixelMatrixConfig = 0;
				PixelMatrixConfigHandle->GetNumChildren(NumChildrenInPixelMatrixConfig);

				for (uint32 IndexPixelMatrixConfigChild = 0; IndexPixelMatrixConfigChild < NumChildrenInPixelMatrixConfig; IndexPixelMatrixConfigChild++)
				{
					TSharedPtr<IPropertyHandle> PixelMatrixConfigChildHandle = PixelMatrixConfigHandle->GetChildHandle(IndexPixelMatrixConfigChild);
					check(PixelMatrixConfigChildHandle.IsValid() && PixelMatrixConfigChildHandle->IsValidHandle());

					PixelMatrixConfigChildHandle->MarkResetToDefaultCustomized();
				}

				// Show only edited modes
				TSharedPtr<IPropertyHandle> NameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));
				check(NameHandle.IsValid() && NameHandle->IsValidHandle());

				TSharedPtr<FDMXFixtureModeItem> ModeItem = MakeShared<FDMXFixtureModeItem>(SharedData, NameHandle);

				TAttribute<EVisibility> VisibilityAttribute =
					TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXEntityFixtureTypeModePropertiesDetails::GetModeVisibility, ModeItem));

				ModePropertiesCategory
					.AddProperty(ModeHandle)
					.ShouldAutoExpand(true)
					.Visibility(VisibilityAttribute);

				// Separator
				ModePropertiesCategory
					.AddCustomRow(LOCTEXT("FixtureTypeModePropertiesDetails.SearchString", "Mode"))
					.Visibility(EVisibility::Hidden)
					[
						SNew(SSeparator)
						.Orientation(Orient_Horizontal)
					];

				// Hide functions
				TSharedPtr<IPropertyHandle> FunctionsHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
				check(FunctionsHandle.IsValid() && FunctionsHandle->IsValidHandle());
				FunctionsHandle->MarkHiddenByCustomization();
			}
		}
	}
}

void FDMXEntityFixtureTypeModePropertiesDetails::OnModesSelected()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->ForceRefresh();
}

void FDMXEntityFixtureTypeModePropertiesDetails::OnNumModesChanged()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->ForceRefresh();
}

EVisibility FDMXEntityFixtureTypeModePropertiesDetails::GetModeVisibility(TSharedPtr<FDMXFixtureModeItem> ModeItem) const
{
	if (ModeItem->IsModeSelected())
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
