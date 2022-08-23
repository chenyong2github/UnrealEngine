// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Widgets/ObjectMixerEditorListMenuContext.h"

#include "LevelEditor.h"
#include "LevelEditorMenuContext.h"
#include "Framework/MultiBox/MultiBoxDefs.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "ToolMenuDelegates.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "UnrealEdGlobals.h"
#include "Views/MainPanel/ObjectMixerEditorMainPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SEditableTextBox.h"

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

TSharedPtr<SWidget> UObjectMixerEditorListMenuContext::BuildContextMenu(const FObjectMixerEditorListMenuContextData& InData)
{
	RegisterContextMenu();

	UObjectMixerEditorListMenuContext* ContextObject = NewObject<UObjectMixerEditorListMenuContext>();
	ContextObject->Data = InData;

	const FToolMenuContext Context(ContextObject);

	// todo: Uncomment all code when Level Editor Changes from 21454357 are pushed
	// First get scene outliner-style context menu
	// FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	// TSharedPtr<ILevelEditor> LevelEditorInstancePtr = LevelEditorModule.GetLevelEditorInstance().Pin();
	//
	// UToolMenu* BaseMenu = LevelEditorInstancePtr->GenerateLevelEditorContextMenu(
	// 		ELevelEditorMenuContext::SceneOutliner, nullptr, FTypedElementHandle());

	// Now generate custom sections to append
	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* CustomMenu = ToolMenus->GenerateMenu(DefaultContextBaseMenuName, Context);

	// for (const FToolMenuSection& Section : CustomMenu->Sections)
	// {
	// 	BaseMenu->Sections.Add(Section);
	// }

	//return ToolMenus->GenerateWidget(BaseMenu);
	return ToolMenus->GenerateWidget(CustomMenu);
}

void UObjectMixerEditorListMenuContext::RegisterContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	
	if (!ToolMenus->IsMenuRegistered(DefaultContextBaseMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(DefaultContextBaseMenuName);
	
		Menu->AddDynamicSection("DynamicCollectionsSection", FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* InMenu)
			{
				UObjectMixerEditorListMenuContext* Context = InMenu->FindContext<UObjectMixerEditorListMenuContext>();
				if (!Context || Context->Data.SelectedItems.Num() == 0)
				{
					return;
				}
	
				FToolMenuSection& Section = InMenu->FindOrAddSection("Collections");
				Section.Label = LOCTEXT("CollectionsSectionName", "Collections");
			
				Section.AddSubMenu(
					"SelectCollectionsSubMenu",
					LOCTEXT("SelectCollectionsSubmenu", "Select or Add Collection"),
					LOCTEXT("SelectCollectionsSubmenu_Tooltip", "Select the collection to which you wish to assign this object."),
					FNewToolMenuDelegate::CreateLambda(
						[Context](UToolMenu* Menu)
						{
							FToolMenuEntry Args;
							Args.Type = EMultiBlockType::Widget;
							Args.MakeCustomWidget.BindLambda(
								[Context](const FToolMenuContext&, const FToolMenuCustomWidgetContext&)
								{
									return SNew(SBox)
											.MinDesiredWidth(200)
											.Padding(8, 0)
											[
												SAssignNew(Context->EditableText, SEditableTextBox)
												.HintText(LOCTEXT("NewCollectionEditableHintText", "Enter a new collection name..."))
												.OnTextCommitted_Static(&UObjectMixerEditorListMenuContext::OnTextCommitted, Context)
												.OnTextChanged_Static(&UObjectMixerEditorListMenuContext::OnTextChanged, Context)
											]
									;
								}
							);
							Menu->AddMenuEntry("NewCollectionInput", Args);
							
							if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
							{
								if (TArray<FName> Collections = MainPanel->GetAllCollectionNames(); Collections.Num() > 0)
								{
									FToolMenuSection& Section = Menu->FindOrAddSection("Collections");
								   Section.Label = LOCTEXT("CollectionsSectionName", "Collections");

								   Collections.StableSort([](const FName& A, const FName B)
								   {
									   return A.LexicalLess(B);
								   });
								
								   for (const FName& Key : Collections)
								   {
									   const FText KeyText = FText::FromName(Key);
									
									   Section.AddMenuEntry(
										   Key,
										   KeyText,
										   FText::Format(LOCTEXT("AddObjectsToCollectionTooltipFormat", "Add selected to collection '{0}'"), KeyText),
										   FSlateIcon(),
										   FUIAction(
											   FExecuteAction::CreateStatic(&UObjectMixerEditorListMenuContext::OnClickCollectionMenuEntry, Key, Context),
											   FCanExecuteAction(),
											   FIsActionChecked::CreateStatic(&UObjectMixerEditorListMenuContext::AreAllObjectsInCollection, Key, Context)
										   ),
										   EUserInterfaceActionType::Check
									   );
								   }
								}
							}
						}
					)
				);
			}
		));
	}
}

void UObjectMixerEditorListMenuContext::OnTextCommitted(const FText& InText, ETextCommit::Type InCommitType, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);
	
	if (InCommitType == ETextCommit::OnEnter)
	{
		UE_LOG(LogTemp, Warning, TEXT("%hs: %s"), __FUNCTION__, *InText.ToString());

		AddObjectsToCollection(*InText.ToString(), Context);
	}
}

void UObjectMixerEditorListMenuContext::OnTextChanged(const FText& InText, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);
	
	if (const FString AsString = InText.ToString(); AsString.Len() > 12)
	{
		Context->EditableText->SetText(FText::FromString(AsString.LeftChop(1)));
	}
}

void UObjectMixerEditorListMenuContext::OnClickCollectionMenuEntry(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	if (AreAllObjectsInCollection(Key, Context))
	{
		RemoveObjectsFromCollection(Key, Context);
	}
	else
	{
		AddObjectsToCollection(Key, Context);
	}
}

void UObjectMixerEditorListMenuContext::AddObjectsToCollection(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
	{
		TSet<FSoftObjectPath> ObjectPaths;

		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : Context->Data.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				ObjectPaths.Add(Object);
			}
		}
		
		MainPanel->AddObjectsToCollection(Key, ObjectPaths);
	}
}

void UObjectMixerEditorListMenuContext::RemoveObjectsFromCollection(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
	{
		TSet<FSoftObjectPath> ObjectPaths;

		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : Context->Data.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				ObjectPaths.Add(Object);
			}
		}
		
		MainPanel->RemoveObjectsFromCollection(Key, ObjectPaths);
	}
}

bool UObjectMixerEditorListMenuContext::AreAllObjectsInCollection(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);

	bool bAreAllSelectedObjectsInCollection = false;

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
	{
		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : Context->Data.SelectedItems)
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

#undef LOCTEXT_NAMESPACE
