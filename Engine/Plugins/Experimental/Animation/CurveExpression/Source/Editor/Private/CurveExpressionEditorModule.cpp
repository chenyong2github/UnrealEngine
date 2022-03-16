// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

namespace UE::CurveExpressionEditor
{

class FModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

}

IMPLEMENT_MODULE(UE::CurveExpressionEditor::FModule, CurveExpressionEditor)
