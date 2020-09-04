// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImporterModule.h"

#include "UnrealUSDWrapper.h"
#include "USDStageImporter.h"

#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"

#define LOCTEXT_NAMESPACE "UsdStageImporterModule"

class FUSDStageImporterModule : public IUsdStageImporterModule
{
public:
	virtual void StartupModule() override
	{
#if USE_USD_SDK
		IUnrealUSDWrapperModule& UnrealUSDWrapperModule = FModuleManager::Get().LoadModuleChecked< IUnrealUSDWrapperModule >(TEXT("UnrealUSDWrapper"));

		USDStageImporter = MakeUnique<UUsdStageImporter>();
#endif // #if USE_USD_SDK
	}

	virtual void ShutdownModule() override
	{
		USDStageImporter.Reset();
	}

	class UUsdStageImporter* GetImporter() override
	{
		return USDStageImporter.Get();
	}

private:
	TUniquePtr<UUsdStageImporter> USDStageImporter;
};

IMPLEMENT_MODULE_USD(FUSDStageImporterModule, USDStageImporter);

#undef LOCTEXT_NAMESPACE
