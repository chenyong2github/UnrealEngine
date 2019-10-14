// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelVariantSetsAssetActions.h"

#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "ToolMenus.h"
#include "LevelVariantSets.h"
#include "LevelVariantSetsEditorToolkit.h"
#include "VariantManagerModule.h"

#define LOCTEXT_NAMESPACE "LevelVariantSetAssetActions"


FLevelVariantSetsAssetActions::FLevelVariantSetsAssetActions( const TSharedRef<ISlateStyle>& InStyle )
	: Style(InStyle)
{

}

uint32 FLevelVariantSetsAssetActions::GetCategories()
{
	return EAssetTypeCategories::Misc;
}

FText FLevelVariantSetsAssetActions::GetName() const
{
	return NSLOCTEXT("AssetTypeActions", "AssetTypeActions_LevelVariantSets", "Level Variant Sets");
}

UClass* FLevelVariantSetsAssetActions::GetSupportedClass() const
{
	return ULevelVariantSets::StaticClass();
}

void FLevelVariantSetsAssetActions::GetActions(const TArray<UObject*>& InObjects, FToolMenuSection& Section)
{
	ULevelVariantSets* SomeLevelVarSets = nullptr;
	for (UObject* Obj : InObjects)
	{
		if (ULevelVariantSets* ObjAsLevelVarSets = Cast<ULevelVariantSets>(Obj))
		{
			SomeLevelVarSets = ObjAsLevelVarSets;
			break;
		}
	}
	if (SomeLevelVarSets == nullptr)
	{
		return;
	}

	Section.AddMenuEntry(
		"CreateActorText",
		LOCTEXT("CreateActorText", "Create LevelVariantSets actor"),
		LOCTEXT("CreateActorTooltip", "Creates a new ALevelVariantSetsActor AActor and add it to the scene"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([SomeLevelVarSets]()
			{
				IVariantManagerModule& VarManModule = IVariantManagerModule::Get();
				VarManModule.GetOrCreateLevelVariantSetsActor(SomeLevelVarSets, true);
			}),
			FCanExecuteAction()
		)
	);
}

FColor FLevelVariantSetsAssetActions::GetTypeColor() const
{
	return FColor(80, 80, 200);
}

void FLevelVariantSetsAssetActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (ULevelVariantSets* LevelVariantSets = Cast<ULevelVariantSets>(*ObjIt))
		{
			TSharedRef<FLevelVariantSetsEditorToolkit> Toolkit = MakeShareable(new FLevelVariantSetsEditorToolkit(Style));
			Toolkit->Initialize(Mode, EditWithinLevelEditor, LevelVariantSets);
		}
	}
}

bool FLevelVariantSetsAssetActions::ShouldForceWorldCentric()
{
	return true;
}


#undef LOCTEXT_NAMESPACE
