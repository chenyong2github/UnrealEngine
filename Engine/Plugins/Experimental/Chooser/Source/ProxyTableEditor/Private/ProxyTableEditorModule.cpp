// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProxyTableEditorModule.h"
#include "IAssetTools.h"
#include "ProxyTableEditor.h"
#include "ProxyTableEditorCommands.h"

#define LOCTEXT_NAMESPACE "ChooserEditorModule"

namespace UE::ProxyTableEditor
{

void FModule::StartupModule()
{
	FProxyTableEditor::RegisterWidgets();

	FProxyTableEditorCommands::Register();
}

void FModule::ShutdownModule()
{
}

}

IMPLEMENT_MODULE(UE::ProxyTableEditor::FModule, ProxyTableEditor);

#undef LOCTEXT_NAMESPACE