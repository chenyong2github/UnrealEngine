// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorModule.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "WaveformEditorCommands.h"
#include "WaveformEditorCustomDetailsHelpers.h"
#include "WaveformEditorDetailsCustomization.h"
#include "WaveformEditorInstantiator.h"
#include "WaveformEditorLog.h"

DEFINE_LOG_CATEGORY(LogWaveformEditor);


void FWaveformEditorModule::StartupModule()
{
	FWaveformEditorCommands::Register();

	WaveformEditorInstantiator = MakeShared<FWaveformEditorInstantiator>();
	RegisterContentBrowserExtensions(WaveformEditorInstantiator.Get());

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		UWaveformTransformationsViewHelper::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<FWaveformTransformationsDetailsCustomization>(); }));
}

void FWaveformEditorModule::ShutdownModule()
{
	FWaveformEditorCommands::Unregister();
}

void FWaveformEditorModule::RegisterContentBrowserExtensions(IWaveformEditorInstantiator* Instantiator)
{
	WaveformEditorInstantiator->ExtendContentBrowserSelectionMenu();
}

IMPLEMENT_MODULE(FWaveformEditorModule, WaveformEditor);
