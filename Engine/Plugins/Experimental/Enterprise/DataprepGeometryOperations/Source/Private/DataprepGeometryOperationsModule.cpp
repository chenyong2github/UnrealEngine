// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "DataprepGeometrySelectionTransforms.h"

class FDataprepGeometryOperationsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked< FPropertyEditorModule >( TEXT("PropertyEditor") );
		PropertyModule.RegisterCustomClassLayout( UDataprepOverlappingActorsSelectionTransform::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic( &FDataprepOverlappingActorsSelectionTransformDetails::MakeDetails ) );
	}

	virtual void ShutdownModule() override
	{
	}
};
  
IMPLEMENT_MODULE( FDataprepGeometryOperationsModule, DataprepGeometryOperations )
