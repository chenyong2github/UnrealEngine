// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataCharts.h"
#include "DataChartsPlacement.h"
#include "DataChartsStyle.h"

#define LOCTEXT_NAMESPACE "FDataChartsModule"

void FDataChartsModule::StartupModule()
{
	FDataChartsStyle::Initialize();
	FDataChartsPlacement::RegisterPlacement();
}

void FDataChartsModule::ShutdownModule()
{
	FDataChartsStyle::Shutdown();
	FDataChartsPlacement::UnregisterPlacement();
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FDataChartsModule, DataCharts)