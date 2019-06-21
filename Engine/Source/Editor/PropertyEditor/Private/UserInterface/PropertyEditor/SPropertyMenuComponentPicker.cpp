// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UserInterface/PropertyEditor/SPropertyMenuComponentPicker.h"
#include "UserInterface/PropertyEditor/SPropertyMenuComponentPicker.h"

#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EditorStyleSet.h"
#include "GameFramework/Actor.h"
#include "AssetData.h"
#include "Editor.h"
#include "Editor/SceneOutliner/Public/SceneOutlinerPublicTypes.h"
#include "Editor/SceneOutliner/Public/SceneOutlinerModule.h"
#include "AssetRegistryModule.h"
#include "UserInterface/PropertyEditor/PropertyEditorAssetConstants.h"
#include "HAL/PlatformApplicationMisc.h"
#include "SceneOutlinerPublicTypes.h"
#include "ITreeItem.h"

#define LOCTEXT_NAMESPACE "PropertyEditor"

void SPropertyMenuComponentPicker::Construct(const FArguments& InArgs)
{
	InitialComponent = InArgs._InitialComponent;
	bAllowClear = InArgs._AllowClear;
	ActorFilter = InArgs._ActorFilter;
	ComponentFilter = InArgs._ComponentFilter;
	OnSet = InArgs._OnSet;
	OnClose = InArgs._OnClose;

	FMenuBuilder MenuBuilder(true, NULL);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("CurrentComponentOperationsHeader", "Current Component"));
	{
		if (InitialComponent)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("EditComponent", "Edit"),
				LOCTEXT("EditComponent_Tooltip", "Edit this component"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SPropertyMenuComponentPicker::OnEdit)));
		}

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyComponent", "Copy"),
			LOCTEXT("CopyComponent_Tooltip", "Copies the component to the clipboard"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SPropertyMenuComponentPicker::OnCopy))
			);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("PasteComponent", "Paste"),
			LOCTEXT("PasteComponent_Tooltip", "Pastes an component from the clipboard to this field"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SPropertyMenuComponentPicker::OnPaste),
				FCanExecuteAction::CreateSP(this, &SPropertyMenuComponentPicker::CanPaste))
			);

		if (bAllowClear)
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearComponent", "Clear"),
				LOCTEXT("ClearComponent_ToolTip", "Clears the component set on this field"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(this, &SPropertyMenuComponentPicker::OnClear))
			);
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("BrowseHeader", "Browse"));
	{
		TSharedPtr<SWidget> MenuContent;

		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>(TEXT("SceneOutliner"));

		SceneOutliner::FInitializationOptions InitOptions;
		InitOptions.Mode = ESceneOutlinerMode::ComponentPicker;
		InitOptions.bFocusSearchBoxWhenOpened = true;

		TSharedRef<SceneOutliner::FOutlinerPredicateFilter> Filter = MakeShareable(new SceneOutliner::FOutlinerPredicateFilter(ActorFilter, SceneOutliner::EDefaultFilterBehaviour::Fail));
		Filter->ComponentPred = ComponentFilter;
		InitOptions.Filters->Add(Filter);

		InitOptions.ColumnMap.Add(SceneOutliner::FBuiltInColumnTypes::Label(), SceneOutliner::FColumnInfo(SceneOutliner::EColumnVisibility::Visible, 0));

		MenuContent =
			SNew(SBox)
			.WidthOverride(PropertyEditorAssetConstants::SceneOutlinerWindowSize.X)
			.HeightOverride(PropertyEditorAssetConstants::SceneOutlinerWindowSize.Y)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
				[
					SceneOutlinerModule.CreateSceneOutliner(InitOptions, FOnSceneOutlinerItemPicked::CreateSP(this, &SPropertyMenuComponentPicker::OnItemSelected))
				]
			];

		MenuBuilder.AddWidget(MenuContent.ToSharedRef(), FText::GetEmpty(), true);

	}
	MenuBuilder.EndSection();

	ChildSlot
	[
		MenuBuilder.MakeWidget()
	];
}

void SPropertyMenuComponentPicker::OnEdit()
{
	if (InitialComponent)
	{
		GEditor->EditObject(InitialComponent);
	}
	OnClose.ExecuteIfBound();
}

void SPropertyMenuComponentPicker::OnCopy()
{
	if (InitialComponent)
	{
		FPlatformApplicationMisc::ClipboardCopy(*FString::Printf(TEXT("%s %s"), *InitialComponent->GetClass()->GetPathName(), *InitialComponent->GetPathName()));
	}
	OnClose.ExecuteIfBound();
}

void SPropertyMenuComponentPicker::OnPaste()
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	bool bFound = false;

	int32 SpaceIndex = INDEX_NONE;
	if (ClipboardText.FindChar(TEXT(' '), SpaceIndex))
	{
		FString ClassPath = ClipboardText.Left(SpaceIndex);
		FString PossibleObjectPath = ClipboardText.Mid(SpaceIndex);

		if (UClass* ClassPtr = LoadClass<UActorComponent>(nullptr, *ClassPath))
		{
			UActorComponent* Component = FindObject<UActorComponent>(nullptr, *PossibleObjectPath);
			if (Component && Component->IsA(ClassPtr) && Component->GetOwner() && (!ComponentFilter.IsBound() || ComponentFilter.Execute(Component)))
			{
				if (!ActorFilter.IsBound() || ActorFilter.Execute(Component->GetOwner()))
				{
					SetValue(Component);
					bFound = true;
				}
			}
		}
	}

	if (!bFound)
	{
		SetValue(nullptr);
	}

	OnClose.ExecuteIfBound();
}

bool SPropertyMenuComponentPicker::CanPaste()
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);

	bool bCanPaste = false;

	int32 SpaceIndex = INDEX_NONE;
	if (ClipboardText.FindChar(TEXT(' '), SpaceIndex))
	{
		FString Class = ClipboardText.Left(SpaceIndex);
		FString PossibleObjectPath = ClipboardText.Mid(SpaceIndex);

		bCanPaste = !Class.IsEmpty() && !PossibleObjectPath.IsEmpty();
		if (bCanPaste)
		{
			bCanPaste = LoadClass<UActorComponent>(nullptr, *Class) != nullptr;
		}
		if (bCanPaste)
		{
			bCanPaste = FindObject<UActorComponent>(nullptr, *PossibleObjectPath) != nullptr;
		}
	}

	return bCanPaste;
}

void SPropertyMenuComponentPicker::OnClear()
{
	SetValue(nullptr);
	OnClose.ExecuteIfBound();
}

void SPropertyMenuComponentPicker::OnItemSelected(TSharedRef<SceneOutliner::ITreeItem> InItem)
{
	InItem->Visit(
		SceneOutliner::FFunctionalVisitor()
		.Component([&](const SceneOutliner::FComponentTreeItem& ComponentItem)
		{
			if (UActorComponent* Component = ComponentItem.Component.Get())
			{
				SetValue(Component);
			}
		}));
	OnClose.ExecuteIfBound();
}

void SPropertyMenuComponentPicker::SetValue(UActorComponent* InComponent)
{
	OnSet.ExecuteIfBound(InComponent);
}

#undef LOCTEXT_NAMESPACE
