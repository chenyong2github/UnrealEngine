// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataLayer/DataLayerPropertyTypeCustomizationHelper.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "DataLayer"

TSharedRef<SWidget> FDataLayerPropertyTypeCustomizationHelper::CreateDataLayerMenu(TFunction<void(const UDataLayer* DataLayer)> OnDataLayerSelectedFunction)
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenDataLayersBrowser", "Browse DataLayers..."),
		FText(),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.DataLayers"),
		FUIAction(
			FExecuteAction::CreateLambda([]()
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
				LevelEditorModule.GetLevelEditorTabManager()->TryInvokeTab(FTabId("LevelEditorDataLayerBrowser"));
			})
		)
	);

	MenuBuilder.BeginSection(FName(), LOCTEXT("ExistingDataLayers", "Existing DataLayers"));
	{
		TArray<TWeakObjectPtr<UDataLayer>> AllDataLayers;
		AllDataLayers.Add(nullptr); // This allows to show the "<None>" option
		UDataLayerEditorSubsystem::Get()->AddAllDataLayersTo(AllDataLayers);

		for (const TWeakObjectPtr<UDataLayer>& WeakDataLayer : AllDataLayers)
		{
			const UDataLayer* DataLayerPtr = WeakDataLayer.Get();
			if (!DataLayerPtr || !DataLayerPtr->IsLocked())
			{
				MenuBuilder.AddMenuEntry(
					UDataLayer::GetDataLayerText(DataLayerPtr),
					FText(),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "DataLayer.Icon16x"),
					FUIAction(FExecuteAction::CreateLambda([DataLayerPtr, OnDataLayerSelectedFunction]() { OnDataLayerSelectedFunction(DataLayerPtr); }))
				);
				}
		}
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE