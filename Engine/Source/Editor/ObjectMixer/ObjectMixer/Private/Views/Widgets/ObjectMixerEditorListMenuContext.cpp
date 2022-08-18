// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Widgets/ObjectMixerEditorListMenuContext.h"

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

	// Build up the menu for a selection
	UToolMenus* ToolMenus = UToolMenus::Get();
	UToolMenu* Menu = ToolMenus->GenerateMenu(DefaultContextBaseMenuName, Context);

	for (const FToolMenuSection& Section : Menu->Sections)
	{
		if (Section.Blocks.Num() > 0)
		{
			return ToolMenus->GenerateWidget(Menu);
		}
	}

	return nullptr;
}

void UObjectMixerEditorListMenuContext::RegisterContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	
	if (!ToolMenus->IsMenuRegistered(DefaultContextBaseMenuName))
	{
		UToolMenu* Menu = ToolMenus->RegisterMenu(DefaultContextBaseMenuName);
	
		Menu->AddDynamicSection("DynamicCategorizationSection", FNewToolMenuDelegate::CreateLambda(
			[](UToolMenu* InMenu)
			{
				UObjectMixerEditorListMenuContext* Context = InMenu->FindContext<UObjectMixerEditorListMenuContext>();
				if (!Context || Context->Data.SelectedItems.Num() == 0)
				{
					return;
				}
	
				FToolMenuSection& Section = InMenu->FindOrAddSection("Categorization");
				Section.Label = LOCTEXT("CategorizationSectionName", "Categorization");
			
				Section.AddSubMenu(
					"SelectCategorySubMenu",
					LOCTEXT("SelectCategorySubmenu", "Select or Add Category"),
					LOCTEXT("SelectCategorySubmenu_Tooltip", "Select the category to which you wish to assign this object."),
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
												.HintText(LOCTEXT("NewCategoryEditableHintText", "Enter a new category name..."))
												.OnTextCommitted_Static(&UObjectMixerEditorListMenuContext::OnTextCommitted, Context)
												.OnTextChanged_Static(&UObjectMixerEditorListMenuContext::OnTextChanged, Context)
											]
									;
								}
							);
							Menu->AddMenuEntry("NewCategoryInput", Args);
							
							if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
							{
								if (TArray<FName> Categories = MainPanel->GetAllCategories().Array(); Categories.Num() > 0)
								{
									FToolMenuSection& Section = Menu->FindOrAddSection("Categories");
								   Section.Label = LOCTEXT("CategoriesSectionName", "Categories");

								   Categories.StableSort([](const FName& A, const FName B)
								   {
									   return A.LexicalLess(B);
								   });
								
								   for (const FName& Key : Categories)
								   {
									   const FText KeyText = FText::FromName(Key);
									
									   Section.AddMenuEntry(
										   Key,
										   KeyText,
										   FText::Format(LOCTEXT("AddObjectsToCategoryTooltipFormat", "Add selected to category '{0}'"), KeyText),
										   FSlateIcon(),
										   FUIAction(
											   FExecuteAction::CreateStatic(&UObjectMixerEditorListMenuContext::OnClickCategoryMenuEntry, Key, Context),
											   FCanExecuteAction(),
											   FIsActionChecked::CreateStatic(&UObjectMixerEditorListMenuContext::AreAllObjectsInCategory, Key, Context)
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

		AddObjectsToCategory(*InText.ToString(), Context);
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

void UObjectMixerEditorListMenuContext::OnClickCategoryMenuEntry(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	if (AreAllObjectsInCategory(Key, Context))
	{
		RemoveObjectsFromCategory(Key, Context);
	}
	else
	{
		AddObjectsToCategory(Key, Context);
	}
}

void UObjectMixerEditorListMenuContext::AddObjectsToCategory(const FName Key, UObjectMixerEditorListMenuContext* Context)
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
		
		MainPanel->AddObjectsToCategory(Key, ObjectPaths);
	}
}

void UObjectMixerEditorListMenuContext::RemoveObjectsFromCategory(const FName Key, UObjectMixerEditorListMenuContext* Context)
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
		
		MainPanel->RemoveObjectsFromCategory(Key, ObjectPaths);
	}
}

bool UObjectMixerEditorListMenuContext::AreAllObjectsInCategory(const FName Key, UObjectMixerEditorListMenuContext* Context)
{
	check(Context);

	bool bAreAllSelectedObjectsInCategory = false;

	if (const TSharedPtr<FObjectMixerEditorMainPanel> MainPanel = Context->Data.MainPanelPtr.Pin())
	{
		for (const TSharedPtr<FObjectMixerEditorListRow>& Item : Context->Data.SelectedItems)
		{
			if (const UObject* Object = Item->GetObject())
			{
				bAreAllSelectedObjectsInCategory = MainPanel->IsObjectInCategory(Key, Object);

				if (!bAreAllSelectedObjectsInCategory)
				{
					return false;
				}
			}
		}
	}

	return bAreAllSelectedObjectsInCategory;
}

#undef LOCTEXT_NAMESPACE
