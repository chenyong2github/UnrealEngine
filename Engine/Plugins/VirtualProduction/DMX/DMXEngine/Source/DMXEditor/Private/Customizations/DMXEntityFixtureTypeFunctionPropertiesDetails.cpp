// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXEntityFixtureTypeFunctionPropertiesDetails.h"

#include "DMXEditor.h"
#include "DMXFixtureTypeSharedData.h"
#include "Library/DMXEntityFixtureType.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "DetailWidgetRow.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SSeparator.h"


#define LOCTEXT_NAMESPACE "DMXEntityFixtureTypeFunctionPropertiesDetails"

void FDMXEntityFixtureTypeFunctionPropertiesDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{	
	DetailBuilder.HideCategory("Entity Properties");
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXImport));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, DMXCategory));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, bFixtureMatrixEnabled));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, InputModulators));

	if (TSharedPtr<FDMXEditor> DMXEditor = DMXEditorPtr.Pin())
	{
		PropertyUtilities = DetailBuilder.GetPropertyUtilities();
		check(PropertyUtilities.IsValid());

		SharedData = DMXEditor->GetFixtureTypeSharedData();
		check(SharedData.IsValid());

		SharedData->OnModesSelected.AddSP(this, &FDMXEntityFixtureTypeFunctionPropertiesDetails::OnModesSelected);
		SharedData->OnFunctionsSelected.AddSP(this, &FDMXEntityFixtureTypeFunctionPropertiesDetails::OnFunctionsSelected);

		// Add all selected Function Properties
		IDetailCategoryBuilder& FunctionPropertiesCategory = DetailBuilder.EditCategory("Function Properties");
		
		TSharedPtr<IPropertyHandle> ModesHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXEntityFixtureType, Modes));
		check(ModesHandle.IsValid() && ModesHandle->IsValidHandle());
		TSharedPtr<IPropertyHandleArray> ModesHandleArray = ModesHandle->AsArray();
		check(ModesHandleArray.IsValid());

		FSimpleDelegate OnNumModesChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeFunctionPropertiesDetails::OnNumModesChanged);
		ModesHandleArray->SetOnNumElementsChanged(OnNumModesChangedDelegate);

		uint32 NumModes = 0;
		if (ModesHandleArray->GetNumElements(NumModes) == FPropertyAccess::Success)
		{
			for (uint32 IndexOfMode = 0; IndexOfMode < NumModes; IndexOfMode++)
			{
				TSharedRef<IPropertyHandle> ModeHandle = ModesHandleArray->GetElement(IndexOfMode);

				TSharedPtr<IPropertyHandle> ModeNameHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, ModeName));
				check(ModeNameHandle.IsValid() && ModeNameHandle->IsValidHandle());

				TSharedPtr<FDMXFixtureModeItem> ModeItem = MakeShared<FDMXFixtureModeItem>(SharedData, ModeNameHandle);
				if (!ModeItem->IsModeSelected())
				{
					// Ignore modes that aren't selected
					continue;
				}

				TSharedPtr<IPropertyHandle> FunctionsHandle = ModeHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXFixtureMode, Functions));
				check(FunctionsHandle.IsValid() && FunctionsHandle->IsValidHandle());
				TSharedPtr<IPropertyHandleArray> FunctionsHandleArray = FunctionsHandle->AsArray();
				check(FunctionsHandleArray.IsValid());
								
				FSimpleDelegate OnNumFunctionsChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXEntityFixtureTypeFunctionPropertiesDetails::OnNumFunctionsChanged);
				FunctionsHandleArray->SetOnNumElementsChanged(OnNumFunctionsChangedDelegate);

				uint32 NumFunctions = 0;
				if (FunctionsHandleArray->GetNumElements(NumFunctions) == FPropertyAccess::Success)
				{
					for (uint32 IndexOfFunction = 0; IndexOfFunction < NumFunctions; IndexOfFunction++)
					{
						TSharedRef<IPropertyHandle> FunctionHandle = FunctionsHandleArray->GetElement(IndexOfFunction);

						TSharedPtr<FDMXFixtureFunctionItem> FunctionItem = MakeShared<FDMXFixtureFunctionItem>(SharedData, ModeNameHandle, FunctionHandle);
						
						TAttribute<EVisibility> VisibilityAttribute =
							TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXEntityFixtureTypeFunctionPropertiesDetails::GetFunctionVisibility, FunctionItem));

						FunctionPropertiesCategory
							.AddProperty(FunctionHandle)
							.ShouldAutoExpand(true)
							.Visibility(VisibilityAttribute);

						// Separator
						FunctionPropertiesCategory
							.AddCustomRow(LOCTEXT("FixtureTypeFunctionPropertiesDetails.SearchString", "Function"))
							.Visibility(VisibilityAttribute)
							[
								SNew(SSeparator)
								.Orientation(Orient_Horizontal)
							];
					}
				}
			}
		}
	}
}

void FDMXEntityFixtureTypeFunctionPropertiesDetails::OnModesSelected()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->RequestRefresh();
}

void FDMXEntityFixtureTypeFunctionPropertiesDetails::OnFunctionsSelected()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->ForceRefresh();
}

void FDMXEntityFixtureTypeFunctionPropertiesDetails::OnNumModesChanged()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->RequestRefresh();
}

void FDMXEntityFixtureTypeFunctionPropertiesDetails::OnNumFunctionsChanged()
{
	check(PropertyUtilities.IsValid());
	PropertyUtilities->RequestRefresh();
}

EVisibility FDMXEntityFixtureTypeFunctionPropertiesDetails::GetFunctionVisibility(TSharedPtr<FDMXFixtureFunctionItem> FunctionItem) const
{
	check(FunctionItem.IsValid());
	if (FunctionItem->IsFunctionSelected())
	{
		return EVisibility::Visible;
	}
	return EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE
