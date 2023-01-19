// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class UNNERuntimeORTDmlImpl;
class UNNERuntimeORTCpuImpl;

class FNNERuntimeORTModule : public IModuleInterface
{

public:
	TWeakObjectPtr<UNNERuntimeORTCpuImpl> NNERuntimeORTCpu{ nullptr };
	TWeakObjectPtr<UNNERuntimeORTDmlImpl> NNERuntimeORTDml{ nullptr };

	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};