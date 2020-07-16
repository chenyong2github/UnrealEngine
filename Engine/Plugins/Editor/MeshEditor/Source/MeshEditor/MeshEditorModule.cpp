// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshEditorModule.h"
#include "EditorModeRegistry.h"
#include "Modules/ModuleManager.h"
#include "MeshEditorMode.h"
#include "MeshEditorStyle.h"
#include "EditorModeManager.h"
#include "ISettingsModule.h"
#include "MeshEditorSettings.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Editor.h"
#include "IMeshEditorModeUIContract.h"
#include "LevelEditor.h"
#include "LevelEditorModesActions.h"


#define LOCTEXT_NAMESPACE "MeshEditor"

class FMeshEditorModule : public IModuleInterface
{
public:
	FMeshEditorModule() :
		bIsEnabled( false ),
		MeshEditorEnable( TEXT( "MeshEditor.Enable" ), TEXT( "Makes MeshEditor mode available" ), FConsoleCommandDelegate::CreateRaw( this, &FMeshEditorModule::Register ) ),
		MeshEditorDisable( TEXT( "MeshEditor.Disable" ), TEXT( "Makes MeshEditor mode unavailable" ), FConsoleCommandDelegate::CreateRaw( this, &FMeshEditorModule::Unregister ) )
	{
	}

	// FModuleInterface overrides
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	virtual bool SupportsDynamicReloading() override
	{
		return true;
	}

protected:

	/** Stores the FEditorMode ID of the associated editor mode */
	static inline FEditorModeID GetEditorModeID()
	{
		static FEditorModeID MeshEditorFeatureName = FName( TEXT( "MeshEditor" ) );
		return MeshEditorFeatureName;
	}

	void Register();
	void Unregister();

	/** Changes the editor mode to the given ID */
	void OnMeshEditModeButtonClicked( EEditableMeshElementType InMode );

	/** Checks whether the editor mode for the given ID is active*/
	ECheckBoxState IsMeshEditModeButtonChecked( EEditableMeshElementType InMode );

	/** Should the mesh edit button be enabled */
	bool IsMeshEditModeButtonEnabled( EEditableMeshElementType InMode );

	/** Whether mesh editor mode is enabled: currently defaults to false */
	bool bIsEnabled;

	/** Console commands for enabling/disabling mesh editor mode while it is still in development */
	FAutoConsoleCommand MeshEditorEnable;
	FAutoConsoleCommand MeshEditorDisable;
};


void FMeshEditorModule::StartupModule()
{
	// Small hack while we're controlling whether mesh editor mode should be enabled on startup or not.
	if( bIsEnabled )
	{
		bIsEnabled = false;
		Register();
	}
}


void FMeshEditorModule::Register()
{
#if ENABLE_MESH_EDITOR
	if( bIsEnabled )
	{
		return;
	}

	bIsEnabled = true;

	FMeshEditorStyle::Initialize();

	FEditorModeRegistry::Get().RegisterMode<FMeshEditorMode>(
		GetEditorModeID(),
		LOCTEXT( "ModeName", "Mesh Editor" ),
		FSlateIcon( FMeshEditorStyle::GetStyleSetName(), "LevelEditor.MeshEditorMode", "LevelEditor.MeshEditorMode.Small" ),
		true,
		600
		);

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>( "Settings" );
	if( SettingsModule )
	{
		// Designer settings
		SettingsModule->RegisterSettings( "Editor", "ContentEditors", "MeshEditor",
										  LOCTEXT("MeshEditorSettingsName", "Mesh Editor"),
										  LOCTEXT("MeshEditorSettingsDescription", "Configure options for the Mesh Editor."),
										  GetMutableDefault<UMeshEditorSettings>()
		);
	}

#endif
}


void FMeshEditorModule::ShutdownModule()
{
	Unregister();
}


void FMeshEditorModule::Unregister()
{
	if( !bIsEnabled )
	{
		return;
	}

	bIsEnabled = false;

	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>( "Settings" );
	if( SettingsModule )
	{
		SettingsModule->UnregisterSettings( "Editor", "ContentEditors", "MeshEditor" );
	}

	FEditorModeRegistry::Get().UnregisterMode(GetEditorModeID());

	FMeshEditorStyle::Shutdown();
}


void FMeshEditorModule::OnMeshEditModeButtonClicked(EEditableMeshElementType InMode)
{
	// *Important* - activate the mode first since FEditorModeTools::DeactivateMode will
	// activate the default mode when the stack becomes empty, resulting in multiple active visible modes.
	GLevelEditorModeTools().ActivateMode(GetEditorModeID());

	// Find and disable any other 'visible' modes since we only ever allow one of those active at a time.
	GLevelEditorModeTools().DeactivateOtherVisibleModes(GetEditorModeID());

	FMeshEditorMode* MeshEditorMode = static_cast<FMeshEditorMode*>( GLevelEditorModeTools().GetActiveMode( GetEditorModeID() ) );
	if ( MeshEditorMode != nullptr)
	{
		IMeshEditorModeUIContract* MeshEditorModeUIContract = (IMeshEditorModeUIContract*)MeshEditorMode;
		MeshEditorModeUIContract->SetMeshElementSelectionMode(InMode);
	}
}

ECheckBoxState FMeshEditorModule::IsMeshEditModeButtonChecked(EEditableMeshElementType InMode)
{
	bool bMeshModeActive = false;

	const FMeshEditorMode* MeshEditorMode = static_cast<FMeshEditorMode*>( GLevelEditorModeTools().GetActiveMode( GetEditorModeID() ) );
	if( MeshEditorMode != nullptr )
	{
		const IMeshEditorModeUIContract* MeshEditorModeUIContract = (const IMeshEditorModeUIContract*)MeshEditorMode;
		bMeshModeActive = MeshEditorModeUIContract->GetMeshElementSelectionMode() == InMode;
	}
	return bMeshModeActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}


bool FMeshEditorModule::IsMeshEditModeButtonEnabled( EEditableMeshElementType InMode )
{
	return true;
}

IMPLEMENT_MODULE( FMeshEditorModule, MeshEditor )

#undef LOCTEXT_NAMESPACE
