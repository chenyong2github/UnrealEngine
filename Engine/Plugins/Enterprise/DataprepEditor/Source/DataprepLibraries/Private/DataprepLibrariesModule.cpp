// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepLibrariesModule.h"
#include "DataprepOperations.h"
#include "DataprepOperationsLibrary.h"

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
