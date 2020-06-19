// Copyright Epic Games, Inc. All Rights Reserved.

#include "MotoSynthDataManager.h"
#include "SynthesisModule.h"

FMotoSynthSourceDataManager& FMotoSynthSourceDataManager::Get()
{
	static FMotoSynthSourceDataManager* MotoSynthDataManager = nullptr;
	if (!MotoSynthDataManager)
	{
		MotoSynthDataManager = new FMotoSynthSourceDataManager();
	}
	return *MotoSynthDataManager;
}

FMotoSynthSourceDataManager::FMotoSynthSourceDataManager()
{

}

FMotoSynthSourceDataManager::~FMotoSynthSourceDataManager()
{

}

void FMotoSynthSourceDataManager::RegisterData(uint32 InSourceID, const TArray<int16>& InSourceDataPCM, int32 InSourceSampleRate, const TArray<FGrainTableEntry>& InGrainTable, const FRichCurve& InRPMCurve)
{
	FScopeLock ScopeLock(&DataCriticalSection);

	if (SourceDataTable.Contains(InSourceID))
	{
		UE_LOG(LogSynthesis, Error, TEXT("Moto synth source data already registered for source ID %d"), InSourceID);
		return;
	}

	MotoSynthDataPtr NewData = MotoSynthDataPtr(new FMotoSynthSourceData);
	NewData->AudioSource = InSourceDataPCM;
	NewData->SourceSampleRate = InSourceSampleRate;
	NewData->GrainTable = InGrainTable;
	NewData->RPMCurve = InRPMCurve;

	SourceDataTable.Add(InSourceID, NewData);
}

void FMotoSynthSourceDataManager::UnRegisterData(uint32 InSourceID)
{
	FScopeLock ScopeLock(&DataCriticalSection);

	// Find the entry
	int32 NumRemoved = SourceDataTable.Remove(InSourceID);
	if (NumRemoved == 0)
	{
		UE_LOG(LogSynthesis, Error, TEXT("No entry in moto synth source data entry for moto synth source ID %d"), InSourceID);
	}
}

MotoSynthDataPtr FMotoSynthSourceDataManager::GetMotoSynthData(uint32 InSourceID)
{
	FScopeLock ScopeLock(&DataCriticalSection);

	MotoSynthDataPtr* SourceData = SourceDataTable.Find(InSourceID);
	if (SourceData)
	{
		return *SourceData;
	}
	else
	{
		UE_LOG(LogSynthesis, Error, TEXT("Unable to get moto synth data view for source ID %d"), InSourceID);
		return nullptr;
	}
}



