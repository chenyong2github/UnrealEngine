// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDExporterModule.h"

#include "LevelExporterUSDOptionsCustomization.h"
#include "USDAssetOptions.h"
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

		// Modify the static mesh LOD range to have the proper define value as the maximum.
		// We have to do this the hard way here because we can't use the define within the meta tag itself
		if ( UScriptStruct* ScriptStruct = FUsdMeshAssetOptions::StaticStruct() )
		{
			if ( FProperty* LowestMeshLODProperty = ScriptStruct->FindPropertyByName( GET_MEMBER_NAME_CHECKED( FUsdMeshAssetOptions, LowestMeshLOD ) ) )
			{
				LowestMeshLODProperty->SetMetaData( TEXT( "ClampMax" ), LexToString( MAX_MESH_LOD_COUNT - 1 ) );
			}

			if ( FProperty* HighestMeshLODProperty = ScriptStruct->FindPropertyByName( GET_MEMBER_NAME_CHECKED( FUsdMeshAssetOptions, HighestMeshLOD ) ) )
			{
				HighestMeshLODProperty->SetMetaData( TEXT( "ClampMax" ), LexToString( MAX_MESH_LOD_COUNT - 1 ) );
			}
		}
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
