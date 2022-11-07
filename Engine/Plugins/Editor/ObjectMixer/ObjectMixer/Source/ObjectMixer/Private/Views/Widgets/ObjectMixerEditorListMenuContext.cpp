// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Widgets/ObjectMixerEditorListMenuContext.h"

#include "ActorFolderPickingMode.h"
#include "FolderTreeItem.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"

#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "LevelEditorContextMenu.h"
#include "LevelEditorMenuContext.h"
#include "LevelEditorViewport.h"
#include "ObjectMixerEditorModule.h"
#include "Selection.h"
#include "SSceneOutliner.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuDelegates.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "ObjectMixerEditorListMenuContext"

FName UObjectMixerEditorListMenuContext::DefaultContextBaseMenuName("ObjectMixer.ContextMenuBase");

TSharedPtr<SWidget> UObjectMixerEditorListMenuContext::CreateContextMenu(const FObjectMixerEditorListMenuContextData InData)
{
	if (InData.SelectedItems.Num() == 0)
	{
		return nullptr;
	}

	return BuildContextMenu(InData);
}

bool UObjectMixerEditorListMenuContext::DoesSelectionHaveType(const FObjectMixerEditorListMenuContextData& InData, UClass* Type)
{
	if (const TSharedPtr<FObjectMixerEditorListRow>* Match = Algo::FindByPredicate(
		InData.SelectedItems,
		[Type](const TSharedPtr<FObjectMixerEditorListRow>& SelectedItem)
		{
			const UObject* Object = SelectedItem->GetObject();
			return Object && Object->IsA(Type);
		}))
	{
		return true;
	}

	return false;
}

void PerformLevelEditorRegistrations(FToolMenuContext& Context)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditorPtr = LevelEditorModule.GetLevelEditorInstance().Pin();
	check(LevelEditorPtr);

	TSharedPtr<FUICommandList> LevelEditorActionsList = LevelEditorPtr->GetLevelEditorActions();
	Context.AppendCommandList(LevelEditorActionsList);

	ULevelEditorContextMenuContext* LevelEditorContextObject = NewObject<ULevelEditorContextMenuContext>();
	LevelEditorContextObject->LevelEditor = LevelEditorPtr;
	LevelEditorContextObject->ContextType = ELevelEditorMenuContext::SceneOutliner;
	LevelEditorContextObject->CurrentSelection = LevelEditorPtr->GetElementSelectionSet();

	for (FSelectedEditableComponentIterator It(GEditor->GetSelectedEditableComponentIterator()); It; ++It)
	{
		LevelEditorContextObject->SelectedComponents.Add(CastChecked<UActorComponent>(*It));
	}

	Context.AddObject(LevelEditorContextObject, [](UObject* InContext)
	{
		ULevelEditorContextMenuContext* CastContext = CastChecked<ULevelEditorContextMenuContext>(InContext);
		CastContext->CurrentSelection = nullptr;
		CastContext->HitProxyElement.Release();
	});

	if (LevelEditorPtr->GetElementSelectionSet()->GetSelectedObjects<UActorComponent>().Num() == 0 &&
		LevelEditorPtr->GetElementSelectionSet()->GetSelectedObjects<AActor>().Num() > 0)
	{
		const TArray<AActor*> SelectedActors = LevelEditorPtr->GetElementSelectionSet()->GetSelectedObjects<AActor>();

		// Get all menu extenders for this context menu from the level editor module
		TArray<FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors> MenuExtenderDelegates =
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders();
		TArray<TSharedPtr<FExtender>> Extenders;
		for (int32 i = 0; i < MenuExtenderDelegates.Num(); ++i)
		{
			if (MenuExtenderDelegates[i].IsBound())
			{
				Extenders.Add(MenuExtenderDelegates[i].Execute(LevelEditorActionsList.ToSharedRef(), SelectedActors));
			}
		}

		if (Extenders.Num() > 0)
		{
			Context.AddExtender(FExtender::Combine(Extenders));
		}
	}
}

TSharedPtr<SWidget> UObjectMixerEditorListMenuContext::BuildContextMenu(const FObjectMixerEditorListMenuContextData& InData)
{	
	FToolMenuContext Context;

	UObjectMixerEditorListMenuContext* ObjectMixerContextObject = NewObject<UObjectMixerEditorListMenuContext>();
	ObjectMixerContextObject->Data = InData;

	Context.AddObject(ObjectMixerContextObject, [](UObject* InContext)
	{
		UObjectMixerEditorListMenuContext* CastContext = CastChecked<UObjectMixerEditorListMenuContext>(InContext);
		CastContext->Data.SelectedItems.Empty();
		CastContext->Data.MainPanelPtr.Reset();
	});

	if (DoesSelectionHaveType(InData, AActor::StaticClass()))
	{
		FLevelEditorContextMenu::RegisterActorContextMenu();
		FLevelEditorContextMenu::RegisterElementContextMenu();
		RegisterObjectMixerActorContextMenuExtension();

		PerformLevelEditorRegistrations(Context);
	
		return UToolMenus::Get()->GenerateWidget("LevelEditor.ActorContextMenu", Context);
	}
	
	if (DoesSelectionHaveType(InData, UActorComponent::StaticClass()))
	{
		FLevelEditorContextMenu::RegisterComponentContextMenu();
		FLevelEditorContextMenu::RegisterElementContextMenu();

		PerformLevelEditorRegistrations(Context);

		return UToolMenus::Get()->GenerateWidget("LevelEditor.ComponentContextMenu", Context);
	}

	if (DoesSelectionHaveType(InData, UObject::StaticClass()))
	{
		FLevelEditorContextMenu::RegisterElementContextMenu();

		PerformLevelEditorRegistrations(Context);

		return UToolMenus::Get()->GenerateWidget("LevelEditor.ElementContextMenu", Context);
	}

	// Folders only
	RegisterFoldersOnlyContextMenu();

	return UToolMenus::Get()->GenerateWidget("ObjectMixer.FoldersOnlyContextMenu", Context);
}

void UObjectMixerEditorListMenuContext::FillSelectionSubMenu(UToolMenu* Menu, const FObjectMixerEditorListMenuContextData& ContextData)
{
	FToolMenuSection& Section = Menu->AddSection("Section");
	Section.AddMenuEntry(
		"AddChildrenToSelection",
		LOCTEXT( "AddChildrenToSelection", "Immediate Children" ),
		LOCTEXT( "AddChildrenToSelection_ToolTip", "Select all immediate children of the selected folders" ),
		FSlateIcon(),
		FExecuteAction::CreateStatic(&UObjectMixerEditorListMenuContext::SelectDescendentsOfSelectedFolders, ContextData, false));
	Section.AddMenuEntry(
		"AddDescendantsToSelection",
		LOCTEXT( "AddDescendantsToSelection", "All Descendants" ),
		LOCTEXT( "AddDescendantsToSelection_ToolTip", "Select all descendants of the selected folders" ),
		FSlateIcon(),
		FExecuteAction::CreateStatic(&UObjectMixerEditorListMenuContext::SelectDescendentsOfSelectedFolders, ContextData, true));
}

void UObjectMixerEditorListMenuContext::RegisterFoldersOnlyContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (ToolMenus->IsMenuRegistered("ObjectMixer.FoldersOnlyContextMenu"))
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->RegisterMenu("ObjectMixer.FoldersOnlyContextMenu");
	Menu->AddDynamicSection("FolderContextMenuDynamic", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
	{
		// Ensure proper context
		UObjectMixerEditorListMenuContext* Context = InMenu->FindContext<UObjectMixerEditorListMenuContext>();
		if (!Context || Context->Data.SelectedItems.Num() == 0)
		{
			return;
		}

		const FObjectMixerEditorListMenuContextData& ContextData = Context->Data;

		{
			FToolMenuSection& Section = InMenu->AddSection("Hierarchy", LOCTEXT("HierarchyMenuHeader", "Hierarchy"));

			if (ContextData.SelectedItems.Num() == 1)
			{
				const FSlateIcon NewFolderIcon(FAppStyle::GetAppStyleSetName(), "SceneOutliner.NewFolderIcon");
				
				Section.AddMenuEntry(
				   "CreateSubFolder",
				   LOCTEXT("CreateSubFolder", "Create Sub Folder"),
				   FText(),
				   NewFolderIcon,
				   FUIAction(FExecuteAction::CreateLambda([ContextData]()
				   {
				   		if (TSharedPtr<FObjectMixerEditorMainPanel> PinnedPanel = ContextData.MainPanelPtr.Pin())
				   		{
				   			PinnedPanel->OnRequestNewFolder(ContextData.SelectedItems[0]->GetFolder());
				   		}
				   }))
			   );
			}

			Section.AddSubMenu(
				"SelectSubMenu",
				LOCTEXT("SelectSubMenu", "Select"),
				FText::GetEmpty(),
				FNewToolMenuDelegate::CreateLambda(
					[ContextData](UToolMenu* InMenu)
				{
					FillSelectionSubMenu(InMenu, ContextData);
				})
			);
		}

		// For future CL
		// {
		// 	FToolMenuSection& Section = InMenu->AddSection("ElementEditActions");
		// 	Section.AddSubMenu(
		// 		"EditSubMenu",
		// 		LOCTEXT("EditSubMenu", "Edit"),
		// 		FText::GetEmpty(),
		// 		FNewToolMenuDelegate::CreateLambda([ContextData](UToolMenu* InMenu)
		// 		{
		// 			FToolMenuSection& Section = InMenu->AddSection("Section");
		// 			Section.AddEntry(MakeCustomEditMenu(ContextData));
		// 		}),
		// 		false, // default value
		// 		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"));
		// }

		GenerateMoveToMenu(InMenu, FToolMenuInsert("Hierarchy", EToolMenuInsertType::After), ContextData);
	}));
}

void UObjectMixerEditorListMenuContext::RegisterObjectMixerActorContextMenuExtension()
{
	if (UToolMenu* ActorContextMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu"))
	{		
		ActorContextMenu->AddDynamicSection("DynamicCollectionsSection", FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* InMenu)
			{
				// Ensure proper context
				UObjectMixerEditorListMenuContext* Context = InMenu->FindContext<UObjectMixerEditorListMenuContext>();
				if (!Context || Context->Data.SelectedItems.Num() == 0)
				{
					return;
				}

				const FObjectMixerEditorListMenuContextData& ContextData = Context->Data;

				ReplaceEditSubMenu(ContextData);

				// Collections submenu
				{
					FToolMenuSection& Section = InMenu->FindOrAddSection("ObjectMixerCollections");
					Section.Label = LOCTEXT("ObjectMixerCollectionsSectionName", "Mixer Collections");
		
					Section.AddSubMenu(
						"SelectCollectionsSubMenu",
						LOCTEXT("SelectCollectionsSubmenu", "Select or Add Collection"),
						LOCTEXT("SelectCollectionsSubmenu_Tooltip", "Select the collection to which you wish to assign this object."),
						FNewToolMenuDelegate::CreateStatic(&UObjectMixerEditorListMenuContext::CreateSelectCollectionsSubMenu, ContextData)
					);
				}

				GenerateMoveToMenu(InMenu, FToolMenuInsert("ActorTypeTools", EToolMenuInsertType::After), ContextData);
			}),
			FToolMenuInsert(NAME_None,EToolMenuInsertType::First)
		);
	}
}

void UObjectMixerEditorListMenuContext::AddCollectionWidget(const FName& Key, const FObjectMixerEditorListMenuContextData& ContextData, UToolMenu* Menu)
{
	const FText KeyText = FText::FromName(Key);

	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox);

	Widget->AddSlot()
	.Padding(FMargin(8, 0))
	.AutoWidth()
	[
		SNew(SCheckBox)
		.OnCheckStateChanged_Lambda([Key, ContextData](ECheckBoxState)
		{
			OnClickCollectionMenuEntry(Key, ContextData);
		})
		.IsChecked_Static(&UObjectMixerEditorListMenuContext::GetCheckStateForCollection, Key, ContextData)
	];

	Widget->AddSlot()
	.AutoWidth()
	[
		SNew(STextBlock)
		.Text(KeyText)
	];
								
	Menu->AddMenuEntry(Key, FToolMenuEntry::InitWidget(Key, Widget, FText(), true));
}

void UObjectMixerEditorListMenuContext::CreateSelectCollectionsSubMenu(UToolMenu* Menu, FObjectMixerEditorListMenuContextData ContextData)
{
	FToolMenuEntry Args;
	Args.Type = EMultiBlockType::Widget;
	Args.MakeCustomWidget.BindLambda(
		[ContextData](const FToolMenuContext&, const FToolMenuCustomWidgetContext&)
		{
			return
				SNew(SBox)
				.MinDesiredWidth(200)
				.Padding(8, 0)
				[
					SNew(SEditableTextBox)
					.HintText(LOCTEXT("NewCollectionEditableHintText", "Enter a new collection name..."))
					.OnTextCommitted_Static(&UObjectMixerEditorListMenuContext::OnTextCommitted, ContextData)
				]
			;
		}
	);
	Menu->AddMenuEntry("NewCollectionInput", Args);
						
	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ContextData.MainPanelPtr.Pin())
	{
		if (TArray<FName> Collections = MainPanel->GetAllCollectionNames(); Collections.Num() > 0)
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("Collections");
			FToolMenuEntry& Separator = Section.AddSeparator("CollectionsSeparator");
			Separator.Label = LOCTEXT("CollectionsSeparatorLabel", "Collections");

			Collections.StableSort([](const FName& A, const FName B)
			{
				return A.LexicalLess(B);
			});
							
			for (const FName& Key : Collections)
			{
				AddCollectionWidget(Key, ContextData, Menu);
			}
		}
	}
}

void UObjectMixerEditorListMenuContext::GenerateMoveToMenu(
	UToolMenu* InMenu, const FToolMenuInsert& InsertArgs, const FObjectMixerEditorListMenuContextData& ContextData)
{
	FToolMenuSection& Section =
		InMenu->AddSection("MainSection",
			LOCTEXT("OutlinerSectionName", "Outliner"), InsertArgs);
					
	Section.AddSubMenu(
			"MoveActorsTo",
			LOCTEXT("MoveActorsTo", "Move To"),
			LOCTEXT("MoveActorsTo_Tooltip", "Move selection to another folder"),
			FNewToolMenuDelegate::CreateStatic(&UObjectMixerEditorListMenuContext::FillFoldersSubMenu, ContextData));
}

void UObjectMixerEditorListMenuContext::OnFoldersMenuFolderSelected(
	TSharedRef<ISceneOutlinerTreeItem> Item,
	FObjectMixerEditorListMenuContextData ContextData)
{
	if (const FFolderTreeItem* FolderTreeItem = Item->CastTo<FFolderTreeItem>())
	{
		for (TSharedPtr<FObjectMixerEditorListRow> SelectedItem : ContextData.SelectedItems)
		{
			if (AActor* AsActor = Cast<AActor>(SelectedItem->GetObject()))
			{
				AsActor->SetFolderPath_Recursively(FolderTreeItem->GetFolder().GetPath());
			}
			else if (SelectedItem->GetRowType() == FObjectMixerEditorListRow::Folder)
			{
				if (TSharedPtr<FObjectMixerEditorMainPanel> PinnedPanel = ContextData.MainPanelPtr.Pin())
				{
					PinnedPanel->OnRequestMoveFolder(SelectedItem->GetFolder(), FolderTreeItem->GetFolder());
				}
			}
		}
	}

	FSlateApplication::Get().DismissAllMenus();
}

TSharedRef<TSet<FFolder>> UObjectMixerEditorListMenuContext::GatherInvalidMoveToDestinations(
	const FObjectMixerEditorListMenuContextData& ContextData)
{
	// We use a pointer here to save copying the whole array for every invocation of the filter delegate
	TSharedRef<TSet<FFolder>> Exclusions(new TSet<FFolder>());

	for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedItem : ContextData.SelectedItems)
	{
		if (SelectedItem->GetRowType() != FObjectMixerEditorListRow::Folder)
		{
			continue;
		}

		Exclusions->Add(SelectedItem->GetFolder());
		
		if (const TSharedPtr<FObjectMixerEditorListRow>& ParentRow = SelectedItem->GetDirectParentRow().Pin())
		{
			auto FolderHasOtherSubFolders = [&SelectedItem](const TSharedPtr<FObjectMixerEditorListRow>& InItem)
			{
				if (InItem->GetRowType() == FObjectMixerEditorListRow::Folder && InItem != SelectedItem)
				{
					return true;
				}
				return false;
			};

			// Exclude this items direct parent if it is a folder and has no other subfolders we can move to
			bool bFolderHasOtherSubFolders = false;
			for (const TSharedPtr<FObjectMixerEditorListRow>& ChildRow : ParentRow->GetChildRows())
			{
				if (FolderHasOtherSubFolders(ChildRow))
				{
					bFolderHasOtherSubFolders = true;
					break;
				}
			}
			
			if (!bFolderHasOtherSubFolders)
			{
				Exclusions->Add(ParentRow->GetFolder());
			}
		}
	}

	return Exclusions;
}

void UObjectMixerEditorListMenuContext::FillFoldersSubMenu(UToolMenu* InMenu, FObjectMixerEditorListMenuContextData ContextData)
{
	FToolMenuSection& Section = InMenu->AddSection("Section");
	Section.AddMenuEntry(
		"CreateNew",
		LOCTEXT( "CreateNew", "Create New Folder" ),
		LOCTEXT( "CreateNew_ToolTip", "Move to a new folder" ),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "SceneOutliner.NewFolderIcon"),
		FExecuteAction::CreateLambda([ContextData]()
		{
			if (const TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = ContextData.MainPanelPtr.Pin())
			{
				PinnedMainPanel->OnRequestNewFolder();
			}
		})
	);
	
	FSceneOutlinerInitializationOptions MiniSceneOutlinerInitOptions;
	MiniSceneOutlinerInitOptions.bShowHeaderRow = false;
	MiniSceneOutlinerInitOptions.bFocusSearchBoxWhenOpened = true;

	MiniSceneOutlinerInitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda(
		[ContextData](SSceneOutliner* Outliner)
			{
				FOnSceneOutlinerItemPicked OnSceneOutlinerItemPicked =
					FOnSceneOutlinerItemPicked::CreateStatic(
						&UObjectMixerEditorListMenuContext::OnFoldersMenuFolderSelected, ContextData
				);

				UWorld* World = FObjectMixerEditorModule::Get().GetWorld();
			
				return new FActorFolderPickingMode(Outliner, OnSceneOutlinerItemPicked, World);
			}
	);

	TSharedRef<TSet<FFolder>> Exclusions = GatherInvalidMoveToDestinations(ContextData);
	MiniSceneOutlinerInitOptions.Filters->AddFilterPredicate<FFolderTreeItem>(
		FFolderTreeItem::FFilterPredicate::CreateLambda(
			[&Exclusions](const FFolder& Folder)
			{
				for (const FFolder& Parent : *Exclusions)
				{
					if (Folder == Parent || Folder.IsChildOf(Parent))
					{
						return false;
					}
				}
				return true;
			}), FSceneOutlinerFilter::EDefaultBehaviour::Pass);

	TSharedRef< SWidget > MiniSceneOutliner =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.MaxHeight(400.0f)
		[
			SNew(SSceneOutliner, MiniSceneOutlinerInitOptions)
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
		];

	FToolMenuSection& MoveToSection = InMenu->AddSection(FName(), LOCTEXT("ExistingFolders", "Existing:"));
	
	MoveToSection.AddEntry(FToolMenuEntry::InitWidget(
		"MiniSceneOutliner",
		MiniSceneOutliner,
		FText::GetEmpty(),
		false)
	);
}

void UObjectMixerEditorListMenuContext::OnTextCommitted(const FText& InText, ETextCommit::Type InCommitType, const FObjectMixerEditorListMenuContextData ContextData)
{
	if (InCommitType == ETextCommit::OnEnter)
	{
		AddObjectsToCollection(*InText.ToString(), ContextData);
	}
}

void UObjectMixerEditorListMenuContext::SelectDescendentsOfSelectedFolders(
	FObjectMixerEditorListMenuContextData ContextData, const bool bRecursive)
{
	for (const TSharedPtr<FObjectMixerEditorListRow>& SelectedRow : ContextData.SelectedItems)
	{
		if (SelectedRow->GetRowType() == FObjectMixerEditorListRow::Folder)
		{
			SelectedRow->SetChildRowsSelected(true, bRecursive);
		}
	}
}

void UObjectMixerEditorListMenuContext::OnClickCollectionMenuEntry(const FName Key, const FObjectMixerEditorListMenuContextData ContextData)
{
	if (AreAllObjectsInCollection(Key, ContextData))
	{
		RemoveObjectsFromCollection(Key, ContextData);
	}
	else
	{
		AddObjectsToCollection(Key, ContextData);
	}
}

void UObjectMixerEditorListMenuContext::AddObjectsToCollection(const FName Key, const FObjectMixerEditorListMenuContextData ContextData)
{
	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ContextData.MainPanelPtr.Pin())
	{
		TSet<FSoftObjectPath> ObjectPaths;

		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : ContextData.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				ObjectPaths.Add(Object);
			}
		}
		
		MainPanel->RequestAddObjectsToCollection(Key, ObjectPaths);
	}
}

void UObjectMixerEditorListMenuContext::RemoveObjectsFromCollection(const FName Key, const FObjectMixerEditorListMenuContextData ContextData)
{
	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ContextData.MainPanelPtr.Pin())
	{
		TSet<FSoftObjectPath> ObjectPaths;

		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : ContextData.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				ObjectPaths.Add(Object);
			}
		}
		
		MainPanel->RequestRemoveObjectsFromCollection(Key, ObjectPaths);
	}
}

bool UObjectMixerEditorListMenuContext::AreAllObjectsInCollection(const FName Key, const FObjectMixerEditorListMenuContextData ContextData)
{
	bool bAreAllSelectedObjectsInCollection = false;

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ContextData.MainPanelPtr.Pin())
	{
		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : ContextData.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				bAreAllSelectedObjectsInCollection = MainPanel->IsObjectInCollection(Key, Object);

				if (!bAreAllSelectedObjectsInCollection)
				{
					return false;
				}
			}
		}
	}

	return bAreAllSelectedObjectsInCollection;
}

ECheckBoxState UObjectMixerEditorListMenuContext::GetCheckStateForCollection(const FName Key,
	const FObjectMixerEditorListMenuContextData ContextData)
{
	const int32 ItemCount = ContextData.SelectedItems.Num();
	int32 ItemsInCollection = 0;
	int32 ItemsNotInCollection = 0;

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = ContextData.MainPanelPtr.Pin())
	{
		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : ContextData.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				if (MainPanel->IsObjectInCollection(Key, Object))
				{
					ItemsInCollection++;
				}
				else
				{
					ItemsNotInCollection++;
				}
			}
		}
	}

	ECheckBoxState ReturnValue = ECheckBoxState::Undetermined;

	if (ItemsInCollection == ItemCount)
	{
		ReturnValue = ECheckBoxState::Checked;
	}
	else if (ItemsNotInCollection == ItemCount)
	{
		ReturnValue = ECheckBoxState::Unchecked;
	}

	return ReturnValue;
}

FToolMenuEntry UObjectMixerEditorListMenuContext::MakeCustomEditMenu(const FObjectMixerEditorListMenuContextData& ContextData)
{
	FToolMenuEntry Entry;
	Entry.Name = TEXT("ObjectMixerGenericCommands");
	Entry.Type = EMultiBlockType::Widget;
	FToolMenuEntryWidgetData WidgetData;
	WidgetData.bNoIndent = true;
	WidgetData.bNoPadding = true;
	Entry.WidgetData = WidgetData;
	Entry.MakeCustomWidget.BindLambda(
		[ContextData](const FToolMenuContext&, const FToolMenuCustomWidgetContext&)
		{
			if (TSharedPtr<FObjectMixerEditorMainPanel> PinnedMainPanel = ContextData.MainPanelPtr.Pin())
			{
				// Add options with our mapped command list to an FMenuBuilder
				FMenuBuilder Builder(true, PinnedMainPanel->ObjectMixerElementEditCommands);
				{
					Builder.AddMenuEntry( FGenericCommands::Get().Cut);
					Builder.AddMenuEntry( FGenericCommands::Get().Copy);
					Builder.AddMenuEntry( FGenericCommands::Get().Paste);
					Builder.AddMenuEntry( FGenericCommands::Get().Duplicate);
					Builder.AddMenuEntry( FGenericCommands::Get().Delete);
					Builder.AddMenuEntry( FGenericCommands::Get().Rename);
				}

				return Builder.MakeWidget();
			}

			return SNullWidget::NullWidget;
		}
	);

	return Entry;
}

void UObjectMixerEditorListMenuContext::ReplaceEditSubMenu(const FObjectMixerEditorListMenuContextData& ContextData)
{
	if (UToolMenu* EditSubMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.ActorContextMenu.EditSubMenu"))
	{
		// Remove existing edit sub menu options
		FCustomizedToolMenu* MenuCustomization = EditSubMenu->AddMenuCustomization();
		{
			MenuCustomization->AddEntry(TEXT("Cut"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
			MenuCustomization->AddEntry(TEXT("Copy"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
			MenuCustomization->AddEntry(TEXT("Paste"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
			MenuCustomization->AddEntry(TEXT("Duplicate"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
			MenuCustomization->AddEntry(TEXT("Delete"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
			MenuCustomization->AddEntry(TEXT("Rename"))->Visibility = ECustomizedToolMenuVisibility::Hidden;
		}

		// Add our menu widget to the edit submenu
		// It must be a widget entry because we hid the entries above, so they won't show as regular menu entries since they share names
		EditSubMenu->AddMenuEntry(NAME_None, MakeCustomEditMenu(ContextData));
	}
}

#undef LOCTEXT_NAMESPACE
