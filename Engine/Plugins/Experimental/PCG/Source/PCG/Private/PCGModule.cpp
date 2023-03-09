// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGModule.h"

#include "PCGEngineSettings.h"

#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Elements/PCGDifferenceElement.h"
#include "Tests/Determinism/PCGDeterminismNativeTests.h"
#include "Tests/Determinism/PCGDifferenceDeterminismTest.h"
#endif

#define LOCTEXT_NAMESPACE "FPCGModule"

class FPCGModule final : public IModuleInterface
{
public:
	//~ IModuleInterface implementation
	virtual void StartupModule() override;

	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

	//~ End IModuleInterface implementation

private:
	void RegisterSettings();
	void UnregisterSettings();

#if WITH_EDITOR
	void RegisterNativeElementDeterminismTests();
	void DeregisterNativeElementDeterminismTests();
#endif
};

void FPCGModule::StartupModule()
{
	RegisterSettings();

#if WITH_EDITOR
	PCGDeterminismTests::FNativeTestRegistry::Create();

	RegisterNativeElementDeterminismTests();
#endif
}

void FPCGModule::ShutdownModule()
{
	UnregisterSettings();

#if WITH_EDITOR
	DeregisterNativeElementDeterminismTests();

	PCGDeterminismTests::FNativeTestRegistry::Destroy();
#endif
}

void FPCGModule::RegisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->RegisterSettings("Project", "Plugins", "PCG",
			LOCTEXT("PCGEngineSettingsName", "PCG"),
			LOCTEXT("PCGEngineSettingsDescription", "Configure PCG."),
			GetMutableDefault<UPCGEngineSettings>());
	}
}

void FPCGModule::UnregisterSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->UnregisterSettings("Project", "Plugins", "PCG");
	}
}

#if WITH_EDITOR

void FPCGModule::RegisterNativeElementDeterminismTests()
{
	PCGDeterminismTests::FNativeTestRegistry::RegisterTestFunction(UPCGDifferenceSettings::StaticClass(), PCGDeterminismTests::DifferenceElement::RunTestSuite);
	// TODO: Add other native test functions
}

void FPCGModule::DeregisterNativeElementDeterminismTests()
{
	PCGDeterminismTests::FNativeTestRegistry::DeregisterTestFunction(UPCGDifferenceSettings::StaticClass());
}

#endif

IMPLEMENT_MODULE(FPCGModule, PCG);

PCG_API DEFINE_LOG_CATEGORY(LogPCG);

#undef LOCTEXT_NAMESPACE
