// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

#include "Templates/SharedPointer.h"

struct FInsightsMajorTabExtender;

class FSourceFilteringEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FString SourceFiltersIni;

protected:
	void RegisterLayoutExtensions(FInsightsMajorTabExtender& InOutExtender);
};
