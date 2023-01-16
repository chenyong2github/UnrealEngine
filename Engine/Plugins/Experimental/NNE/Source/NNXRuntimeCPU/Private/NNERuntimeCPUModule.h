// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

namespace NNX { class IRuntime; }
class UNNERuntimeCPUImpl;

class FNNERuntimeCPUModule : public IModuleInterface
{

public:
	NNX::IRuntime* CPURuntime{ nullptr };
	TWeakObjectPtr<UNNERuntimeCPUImpl> NNERuntimeCPU{ nullptr };

	// Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};