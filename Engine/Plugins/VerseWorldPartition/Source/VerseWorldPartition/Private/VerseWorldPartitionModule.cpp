// Copyright Epic Games, Inc. All Rights Reserved.

#include "VerseWorldPartitionModule.h"
#include "Modules/ModuleManager.h"
#include "VerseWorldPartition.h"

#define LOCTEXT_NAMESPACE "FVerseWorldPartitionModule"

class FVerseWorldPartitionModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IVerseWorldPartitionModule& IVerseWorldPartitionModule::Get()
{
	return FModuleManager::GetModuleChecked<IVerseWorldPartitionModule>(TEXT("VerseWorldPartition"));
}
	
IMPLEMENT_MODULE(FVerseWorldPartitionModule, VerseWorldPartition)

#undef LOCTEXT_NAMESPACE