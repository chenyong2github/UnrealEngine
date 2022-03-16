// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RigVM.h: Module implementation.
=============================================================================*/

#include "RigVMModule.h"
#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, RigVM);

DEFINE_LOG_CATEGORY(LogRigVM);

#if UE_RIGVM_UOBJECT_PROPERTIES_ENABLED
TAutoConsoleVariable<bool> CVarRigVMEnableUObjects(TEXT("RigVM.UObjectSupport"), true, TEXT("When true the RigVMCompiler will allow UObjects."));
#endif

bool RigVMCore::SupportsUObjects()
{
#if UE_RIGVM_UOBJECT_PROPERTIES_ENABLED
	return CVarRigVMEnableUObjects.GetValueOnGameThread();
#else
	return false;
#endif
}
