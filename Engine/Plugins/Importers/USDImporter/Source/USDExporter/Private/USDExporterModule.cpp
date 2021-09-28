// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterModule.h"

#include "LevelExporterUSDOptionsCustomization.h"
#include "USDMemory.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FUsdExporterModule : public IUsdExporterModule
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT( "PropertyEditor" ) );

		// We intentionally use the same customization for both of these
		PropertyModule.RegisterCustomClassLayout( TEXT( "LevelExporterUSDOptions" ), FOnGetDetailCustomizationInstance::CreateStatic( &FLevelExporterUSDOptionsCustomization::MakeInstance ) );
		PropertyModule.RegisterCustomClassLayout( TEXT( "LevelSequenceExporterUSDOptions" ), FOnGetDetailCustomizationInstance::CreateStatic( &FLevelExporterUSDOptionsCustomization::MakeInstance ) );
	}

	virtual void ShutdownModule() override
	{
		if ( FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr< FPropertyEditorModule >( TEXT( "PropertyEditor" ) ) )
		{
			PropertyModule->UnregisterCustomClassLayout( TEXT( "LevelExporterUSDOptions" ) );
			PropertyModule->UnregisterCustomClassLayout( TEXT( "LevelSequenceExporterUSDOptions" ) );
		}
	}
};

IMPLEMENT_MODULE_USD( FUsdExporterModule, USDExporter );
