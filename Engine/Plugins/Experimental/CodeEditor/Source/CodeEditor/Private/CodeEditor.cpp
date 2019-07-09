// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Textures/SlateIcon.h"
#include "Framework/Commands/UIAction.h"
#include "EditorMenuSubsystem.h"
#include "Framework/Docking/TabManager.h"
#include "Toolkits/IToolkitHost.h"
#include "CodeEditorStyle.h"
#include "CodeProject.h"
#include "CodeProjectEditor.h"
#include "LevelEditor.h"

static const FName CodeEditorTabName( TEXT( "CodeEditor" ) );

#define LOCTEXT_NAMESPACE "CodeEditor"

class FCodeEditor : public IModuleInterface
{
private:
	static TSharedRef<SDockTab> SpawnCodeEditorTab(const FSpawnTabArgs& TabArgs)
	{
		TSharedRef<FCodeProjectEditor> NewCodeProjectEditor(new FCodeProjectEditor());
		NewCodeProjectEditor->InitCodeEditor(EToolkitMode::Standalone, TSharedPtr<class IToolkitHost>(), GetMutableDefault<UCodeProject>());

		return FGlobalTabmanager::Get()->GetMajorTabForTabManager(NewCodeProjectEditor->GetTabManager().ToSharedRef()).ToSharedRef();
	}

	static void OpenCodeEditor()
	{
		SpawnCodeEditorTab(FSpawnTabArgs(TSharedPtr<SWindow>(), FTabId()));	
	}

public:

	virtual void OnPostEngineInit()
	{
		UEditorMenu* Menu = UEditorMenuSubsystem::Get()->ExtendMenu("LevelEditor.MainMenu.File");
		FEditorMenuSection& Section = Menu->FindOrAddSection("FileProject");

		FEditorMenuOwnerScoped OwnerScoped(this);
		{
			FEditorMenuEntry& MenuEntry = Section.AddMenuEntry(
				"EditSourceCode",
				LOCTEXT("CodeEditorTabTitle", "Edit Source Code"),
				LOCTEXT("CodeEditorTooltipText", "Open the Code Editor tab."),
				FSlateIcon(FCodeEditorStyle::Get().GetStyleSetName(), "CodeEditor.TabIcon"),
				FUIAction
				(
					FExecuteAction::CreateStatic(&FCodeEditor::OpenCodeEditor)
				)
			);
			MenuEntry.InsertPosition = FEditorMenuInsert(NAME_None, EEditorMenuInsertType::First);
		}
	}

	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		FCodeEditorStyle::Initialize();

		// Register a tab spawner so that our tab can be automatically restored from layout files
		FGlobalTabmanager::Get()->RegisterTabSpawner( CodeEditorTabName, FOnSpawnTab::CreateStatic( &FCodeEditor::SpawnCodeEditorTab ) )
				.SetDisplayName( LOCTEXT( "CodeEditorTabTitle", "Edit Source Code" ) )
				.SetTooltipText( LOCTEXT( "CodeEditorTooltipText", "Open the Code Editor tab." ) )
				.SetIcon(FSlateIcon(FCodeEditorStyle::Get().GetStyleSetName(), "CodeEditor.TabIcon"));

		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FCodeEditor::OnPostEngineInit);
	}

	virtual void ShutdownModule() override
	{
		// Unregister the tab spawner
		FGlobalTabmanager::Get()->UnregisterTabSpawner( CodeEditorTabName );

		FCoreDelegates::OnPostEngineInit.RemoveAll(this);
		UEditorMenuSubsystem::UnregisterOwner(this);

		FCodeEditorStyle::Shutdown();
	}
};

IMPLEMENT_MODULE( FCodeEditor, CodeEditor )

#undef LOCTEXT_NAMESPACE
