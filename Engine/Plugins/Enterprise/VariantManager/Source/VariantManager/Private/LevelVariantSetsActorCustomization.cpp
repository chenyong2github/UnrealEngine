// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LevelVariantSetsActorCustomization.h"

#include "CoreMinimal.h"
#include "AssetToolsModule.h"
#include "LevelVariantSetsActor.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IAssetTools.h"
#include "LevelVariantSets.h"
#include "Modules/ModuleManager.h"
#include "UObject/Object.h"
#include "VariantManagerLog.h"
#include "VariantManagerModule.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SButton.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "LevelVariantSetsActorCustomization"

FLevelVariantSetsActorCustomization::FLevelVariantSetsActorCustomization()
{
}

TSharedRef<IDetailCustomization> FLevelVariantSetsActorCustomization::MakeInstance()
{
	return MakeShared<FLevelVariantSetsActorCustomization>();
}

void FLevelVariantSetsActorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayoutBuilder)
{
	//Default to standard details panel when multiple variantsselectors are selected at once
	TArray<TWeakObjectPtr<UObject>> SelectedObjects = DetailLayoutBuilder.GetSelectedObjects();
	if (SelectedObjects.Num() != 1)
	{
		return;
	}

	TWeakObjectPtr<UObject> SelectedObject = SelectedObjects[0];
	if (!SelectedObject.IsValid())
	{
		return;
	}

	ALevelVariantSetsActor* Actor = Cast<ALevelVariantSetsActor>(SelectedObject.Get());

	IDetailCategoryBuilder& ActionsCategory = DetailLayoutBuilder.EditCategory(TEXT("VariantManager"));

	ActionsCategory.AddCustomRow(FText())
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.Text(LOCTEXT("OpenVariantManager", "Open Variant Manager"))
			.OnClicked(this, &FLevelVariantSetsActorCustomization::OnOpenVariantManagerButtonClicked, Actor)
		]
	];

	ActionsCategory.AddCustomRow(FText())
	.WholeRowContent()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.Text(LOCTEXT("NewLevelVarSets", "Create new LevelVariantSets asset"))
			.OnClicked(this, &FLevelVariantSetsActorCustomization::OnCreateLevelVarSetsButtonClicked)
		]
	];

	ActionsCategory.AddProperty(DetailLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ALevelVariantSetsActor, LevelVariantSets)));
}

FReply FLevelVariantSetsActorCustomization::OnOpenVariantManagerButtonClicked(ALevelVariantSetsActor* Actor)
{
	if (Actor == nullptr || !Actor->IsValidLowLevel())
	{
		return FReply::Unhandled();
	}

	ULevelVariantSets* LevelVarSets = Actor->GetLevelVariantSets(true);
	if (LevelVarSets == nullptr || !LevelVarSets->IsValidLowLevel())
	{
		return FReply::Unhandled();
	}

	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets({LevelVarSets});

	return FReply::Handled();
}

FReply FLevelVariantSetsActorCustomization::OnCreateLevelVarSetsButtonClicked()
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	IVariantManagerModule& VarManModule = IVariantManagerModule::Get();

	UObject* NewAsset = VarManModule.CreateLevelVariantSetsAssetWithDialog();
	if (!NewAsset)
	{
		UE_LOG(LogVariantManager, Error, TEXT("Did not find an asset factory for a ULevelVariantSets"));
		return FReply::Unhandled();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE