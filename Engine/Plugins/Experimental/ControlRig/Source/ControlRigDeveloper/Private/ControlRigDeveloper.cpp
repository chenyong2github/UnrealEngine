// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigDeveloper.h"
#include "ControlRigBlueprint.h"
#include "ControlRigBlueprintCompiler.h"
#include "Modules/ModuleManager.h"

class FControlRigDeveloperModule : public IControlRigDeveloperModule
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** Compiler customization for animation controllers */
	FControlRigBlueprintCompiler ControlRigBlueprintCompiler;

	static TSharedPtr<FKismetCompilerContext> GetControlRigCompiler(UBlueprint* BP, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
};

void FControlRigDeveloperModule::StartupModule()
{
	// Register blueprint compiler
	FKismetCompilerContext::RegisterCompilerForBP(UControlRigBlueprint::StaticClass(), &FControlRigDeveloperModule::GetControlRigCompiler);
	IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
	KismetCompilerModule.GetCompilers().Add(&ControlRigBlueprintCompiler);

}

void FControlRigDeveloperModule::ShutdownModule()
{
	IKismetCompilerInterface* KismetCompilerModule = FModuleManager::GetModulePtr<IKismetCompilerInterface>("KismetCompiler");
	if (KismetCompilerModule)
	{
		KismetCompilerModule->GetCompilers().Remove(&ControlRigBlueprintCompiler);
	}
}

TSharedPtr<FKismetCompilerContext> FControlRigDeveloperModule::GetControlRigCompiler(UBlueprint* BP, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
{
	return TSharedPtr<FKismetCompilerContext>(new FControlRigBlueprintCompilerContext(BP, InMessageLog, InCompileOptions));
}


IMPLEMENT_MODULE(FControlRigDeveloperModule, ControlRigDeveloper)
