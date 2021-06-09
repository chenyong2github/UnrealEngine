// Copyright Epic Games, Inc. All Rights Reserved.

#include "PLUGIN_NAMEEdMode.h"
#include "PLUGIN_NAMEEdModeToolkit.h"
#include "Toolkits/ToolkitManager.h"
#include "EditorModeManager.h"

#define LOCTEXT_NAMESPACE "PLUGIN_NAME"

const FEditorModeID UPLUGIN_NAMEEdMode::EM_PLUGIN_NAMEEdModeId = TEXT("EM_PLUGIN_NAMEEdMode");

UPLUGIN_NAMEEdMode::UPLUGIN_NAMEEdMode()
	: Super()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	Info = FEditorModeInfo(
		FName(TEXT("PLUGIN_NAME")),
		LOCTEXT("ModeName", "PLUGIN_NAME Editor Mode"),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.MeshPaintMode", "LevelEditor.MeshPaintMode.Small"),
		true,
		600
	);
}


void UPLUGIN_NAMEEdMode::Enter()
{
	Super::Enter();

	if (!Toolkit.IsValid() && UsesToolkits())
	{
		Toolkit = MakeShareable(new FPLUGIN_NAMEEdModeToolkit);
		Toolkit->Init(Owner->GetToolkitHost());
	}
}

void UPLUGIN_NAMEEdMode::Exit()
{
	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	// Call base Exit method to ensure proper cleanup
	Super::Exit();
}

void UPLUGIN_NAMEEdMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FPLUGIN_NAMEEdModeToolkit);
}

#undef LOCTEXT_NAMESPACE
