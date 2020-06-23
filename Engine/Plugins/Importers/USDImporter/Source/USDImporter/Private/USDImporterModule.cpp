// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDImporterPrivatePCH.h"
#include "Misc/Paths.h"
#include "UObject/ObjectMacros.h"
#include "UObject/GCObject.h"
#include "USDImporter.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "USDImporterProjectSettings.h"

#include "USDMemory.h"

#define LOCTEXT_NAMESPACE "USDImportPlugin"

class FUSDImporterModule : public IUSDImporterModule, public FGCObject
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override
	{
		IUnrealUSDWrapperModule& UnrealUSDWrapperModule = FModuleManager::Get().LoadModuleChecked< IUnrealUSDWrapperModule >( TEXT("UnrealUSDWrapper") );

		USDImporter = NewObject<UDEPRECATED_UUSDImporter>();
	}

	virtual void ShutdownModule() override
	{
		USDImporter = nullptr;
	}

	class UDEPRECATED_UUSDImporter* GetImporter() override
	{
		return USDImporter;
	}

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(USDImporter);
	}
private:
	UDEPRECATED_UUSDImporter* USDImporter;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE_USD(FUSDImporterModule, USDImporter)
