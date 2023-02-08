// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProxyTableEditorModule.h"
#include "IAssetTools.h"
#include "ProxyTableEditor.h"

#define LOCTEXT_NAMESPACE "ChooserEditorModule"

namespace UE::ProxyTableEditor
{

void FModule::StartupModule()
{
	FProxyTableEditor::RegisterWidgets();
}

void FModule::ShutdownModule()
{
}

}

IMPLEMENT_MODULE(UE::ProxyTableEditor::FModule, ProxyTableEditor);

#undef LOCTEXT_NAMESPACE