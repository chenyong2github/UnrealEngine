// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXFixturePatchInspector.h"

#include "DMXEditor.h"
#include "DMXFixturePatchSharedData.h"

#include "Customizations/DMXEditorPropertyEditorCustomization.h"
#include "Library/DMXEntityFixturePatch.h"


void SDMXFixturePatchInspector::Construct(const FArguments& InArgs)
{
	SDMXEntityInspector::Construct(SDMXEntityInspector::FArguments()
		.DMXEditor(InArgs._DMXEditor)
		.OnFinishedChangingProperties(InArgs._OnFinishedChangingProperties)
	);

	if (TSharedPtr<FDMXEditor> PinnedEditor = InArgs._DMXEditor.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = PinnedEditor->GetFixturePatchSharedData();
		check(SharedData.IsValid());

		SharedData->OnFixturePatchSelectionChanged.AddSP(this, &SDMXFixturePatchInspector::OnFixturePatchesSelected);

		OnFixturePatchesSelected();
	}

	// Register customization for UOBJECT
	FOnGetDetailCustomizationInstance FixturePatchDetails = FOnGetDetailCustomizationInstance::CreateStatic(&FDMXCustomizationFactory::MakeInstance<FDMXFixturePatchesDetails>, InArgs._DMXEditor);
	GetPropertyView()->RegisterInstancedCustomPropertyLayout(UDMXEntityFixturePatch::StaticClass(), FixturePatchDetails);
}

void SDMXFixturePatchInspector::OnFixturePatchesSelected()
{
	if (TSharedPtr<FDMXEditor> PinnedEditor = DMXEditor.Pin())
	{
		TSharedPtr<FDMXFixturePatchSharedData> SharedData = PinnedEditor->GetFixturePatchSharedData();
		check(SharedData);

		TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> SelectedFixturePatches = SharedData->GetSelectedFixturePatches();

		TArray<UDMXEntity*> EntityArr;
		for (TWeakObjectPtr<UDMXEntityFixturePatch> Patch : SelectedFixturePatches)
		{
			if (Patch.IsValid())
			{
				EntityArr.Add(Patch.Get());
			}
		}

		ShowDetailsForEntities(EntityArr);
	}
}
