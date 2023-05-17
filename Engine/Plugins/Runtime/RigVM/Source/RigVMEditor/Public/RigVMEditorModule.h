// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RigVMEditor.h: Module definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRigVMEditor, Log, All);

class FRigVMEditorModule : public IModuleInterface
{
public:

	static FRigVMEditorModule& Get();

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};