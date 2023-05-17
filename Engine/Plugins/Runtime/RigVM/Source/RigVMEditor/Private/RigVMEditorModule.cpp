// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RigVMEditor.cpp: Module implementation.
=============================================================================*/

#include "RigVMEditorModule.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogRigVMEditor);

IMPLEMENT_MODULE(FRigVMEditorModule, RigVMEditor)

FRigVMEditorModule& FRigVMEditorModule::Get()
{
	return FModuleManager::LoadModuleChecked< FRigVMEditorModule >(TEXT("RigVMEditor"));
}

void FRigVMEditorModule::StartupModule()
{
}

void FRigVMEditorModule::ShutdownModule()
{
}
