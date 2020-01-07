// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataPrepLibrariesModule.h"
#include "DataprepOperations.h"
#include "DataPrepOperationsLibrary.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

class FDataprepLibrariesModule : public IDataprepLibrariesModule
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
		PropertyModule.RegisterCustomClassLayout( UDataprepSetLODGroupOperation::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic( &FDataprepSetLOGGroupDetails::MakeDetails ) );
	}

	virtual void ShutdownModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
		PropertyModule.UnregisterCustomClassLayout( TEXT("DataprepSetLODGroupOperation") );
	}
};

IMPLEMENT_MODULE( FDataprepLibrariesModule, DataprepLibraries )
