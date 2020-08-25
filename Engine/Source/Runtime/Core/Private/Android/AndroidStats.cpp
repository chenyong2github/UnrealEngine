// Copyright Epic Games, Inc. All Rights Reserved.

#include "Android/AndroidStats.h"
#include "CoreMinimal.h"
#include "Android/AndroidPlatformMisc.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"

DECLARE_STATS_GROUP(TEXT("Android CPU stats"), STATGROUP_AndroidCPU, STATCAT_Advanced);
CSV_DEFINE_CATEGORY(AndroidCPU, true);
CSV_DEFINE_CATEGORY(AndroidMemory, true);

DECLARE_DWORD_COUNTER_STAT(TEXT("Num Frequency Groups"), STAT_NumFreqGroups, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 0 : Max frequency"), STAT_FreqGroup0MaxFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 0 : Min frequency"), STAT_FreqGroup0MinFrequency, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 0 : % of max frequency"), STAT_FreqGroup0CurrentFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 0 : Num Cores"), STAT_FreqGroup0NumCores, STATGROUP_AndroidCPU);
CSV_DEFINE_STAT(AndroidCPU, CPUFreqGroup0);

DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 1 : Max frequency"), STAT_FreqGroup1MaxFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 1 : Min frequency"), STAT_FreqGroup1MinFrequency, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 1 : % of max frequency"), STAT_FreqGroup1CurrentFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 1 : Num Cores"), STAT_FreqGroup1NumCores, STATGROUP_AndroidCPU);
CSV_DEFINE_STAT(AndroidCPU, CPUFreqGroup1);

DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 2 : Max frequency"), STAT_FreqGroup2MaxFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 2 : Min frequency"), STAT_FreqGroup2MinFrequency, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 2 : % of max frequency"), STAT_FreqGroup2CurrentFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 2 : Num Cores"), STAT_FreqGroup2NumCores, STATGROUP_AndroidCPU);
CSV_DEFINE_STAT(AndroidCPU, CPUFreqGroup2);

DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 3 : Max frequency"), STAT_FreqGroup3MaxFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 3 : Min frequency"), STAT_FreqGroup3MinFrequency, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 3 : % of max frequency"), STAT_FreqGroup3CurrentFrequency, STATGROUP_AndroidCPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("Freq Group 3 : Num Cores"), STAT_FreqGroup3NumCores, STATGROUP_AndroidCPU);
CSV_DEFINE_STAT(AndroidCPU, CPUFreqGroup3);

DECLARE_DWORD_COUNTER_STAT(TEXT("Num CPU Cores"), STAT_NumCPUCores, STATGROUP_AndroidCPU);

DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 0 : highest core utilization %"), STAT_FreqGroup0MaxUtilization, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 1 : highest core utilization %"), STAT_FreqGroup1MaxUtilization, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 2 : highest core utilization %"), STAT_FreqGroup2MaxUtilization, STATGROUP_AndroidCPU);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Freq Group 3 : highest core utilization %"), STAT_FreqGroup3MaxUtilization, STATGROUP_AndroidCPU);

CSV_DEFINE_STAT(AndroidCPU, CPUTemp);
DECLARE_FLOAT_COUNTER_STAT(TEXT("CPU Temperature"), STAT_CPUTemp, STATGROUP_AndroidCPU);

CSV_DEFINE_STAT(AndroidCPU, ThermalStatus);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Thermal Status"), STAT_ThermalStatus, STATGROUP_AndroidCPU);

#if STATS

#define SET_DWORD_STAT_BY_FNAME(Stat, Amount) \
{\
	if (Amount != 0 && FThreadStats::IsCollectingData()) \
	{ \
		FThreadStats::AddMessage(Stat, EStatOperation::Set, int64(Amount));\
		TRACE_STAT_SET(Stat, int64(Amount)); \
	} \
}

#define SET_FLOAT_STAT_BY_FNAME(Stat, Amount) \
{\
	if (Amount != 0 && FThreadStats::IsCollectingData()) \
	{ \
		FThreadStats::AddMessage(Stat, EStatOperation::Set, double(Amount));\
		TRACE_STAT_SET(Stat, double(Amount)); \
	} \
}

#endif

#if CSV_PROFILER

#define CSV_STAT_PTR(StatName)									&_GCsvStat_##StatName
#define CSV_CUSTOM_STAT_DEFINED_BY_PTR(StatPtr,Value,Op)		FCsvProfiler::RecordCustomStat(StatPtr->Name, StatPtr->CategoryIndex, Value, Op);
FCsvDeclaredStat* GCPUFreqStats[] = {
	CSV_STAT_PTR(CPUFreqGroup0),
	CSV_STAT_PTR(CPUFreqGroup1),
	CSV_STAT_PTR(CPUFreqGroup2),
	CSV_STAT_PTR(CPUFreqGroup3)
};
#undef CSV_STAT_PTR

#else
#define CSV_CUSTOM_STAT_DEFINED_BY_PTR(StatPtr,Value,Op)
#endif

static float GAndroidCPUStatsUpdateRate = 0.100;
static FAutoConsoleVariableRef CVarAndroidCollectCPUStatsRate(
	TEXT("Android.CPUStatsUpdateRate"),
	GAndroidCPUStatsUpdateRate,
	TEXT("Update rate in seconds for collecting CPU Stats (Default: 0.1)\n")
	TEXT("0 to disable."),
	ECVF_Default);

static int GThermalStatus = 0;
static int GMemoryWarningStatus = 0;
CSV_DEFINE_STAT(AndroidMemory, MemoryWarningState);

void FAndroidStats::OnThermalStatusChanged(int status)
{
	GThermalStatus = status;
}

void FAndroidStats::OnMemoryWarningChanged(int status)
{
	GMemoryWarningStatus = status;
}

void FAndroidStats::UpdateAndroidStats()
{
	if (GAndroidCPUStatsUpdateRate <= 0.0f)
	{
		return;
	}

	static float CPUTemp = 0.0f;
	static uint64 LastCollectionTime = FPlatformTime::Cycles64();
	const uint64 CurrentTime = FPlatformTime::Cycles64();
	const bool bUpdateStats = ((FPlatformTime::ToSeconds64(CurrentTime - LastCollectionTime) >= GAndroidCPUStatsUpdateRate));
	if (bUpdateStats)
	{
		LastCollectionTime = CurrentTime;
		CPUTemp = FAndroidMisc::GetCPUTemperature();
	}

	CSV_CUSTOM_STAT_DEFINED(CPUTemp, CPUTemp, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(ThermalStatus, GThermalStatus, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT_DEFINED(MemoryWarningState, GMemoryWarningStatus, ECsvCustomStatOp::Set);

	static const uint32 MaxFrequencyGroupStats = 4;
	const int32 MaxCoresStatsSupport = 16;
	int32 NumCores = FMath::Min(FAndroidMisc::NumberOfCores(), MaxCoresStatsSupport);

	struct FFrequencyGroup
	{
		uint32 MinFrequency;
		uint32 MaxFrequency;
		uint32 CoreCount;
	};

	static uint32 UnInitializedCores = NumCores;
	static TArray<FFrequencyGroup> FrequencyGroups;
	static uint32 CoreFrequencyGroupIndex[MaxCoresStatsSupport] = {
		0xFFFFFFFF, 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
		0xFFFFFFFF, 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
		0xFFFFFFFF, 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
		0xFFFFFFFF, 0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,
	};

	if (UnInitializedCores != 0)
	{
		for (int32 CoreIndex = 0; CoreIndex < NumCores; CoreIndex++)
		{
			if (CoreFrequencyGroupIndex[CoreIndex] == 0xFFFFFFFF)
			{
				uint32 MinFreq = FAndroidMisc::GetCoreFrequency(CoreIndex, FAndroidMisc::ECoreFrequencyProperty::MinFrequency);
				uint32 MaxFreq = FAndroidMisc::GetCoreFrequency(CoreIndex, FAndroidMisc::ECoreFrequencyProperty::MaxFrequency);
				if (MaxFreq > 0)
				{
					UnInitializedCores--;
					int32 FoundIndex = FrequencyGroups.IndexOfByPredicate([&MinFreq, &MaxFreq](const FFrequencyGroup& s) { return s.MinFrequency == MinFreq && s.MaxFrequency == MaxFreq; });
					if (FoundIndex == INDEX_NONE)
					{
						FFrequencyGroup NewGroup = { MinFreq,MaxFreq, 1 };
						CoreFrequencyGroupIndex[CoreIndex] = FrequencyGroups.Add(NewGroup);
					}
					else
					{
						CoreFrequencyGroupIndex[CoreIndex] = FoundIndex;
						FrequencyGroups[FoundIndex].CoreCount++;
					}
				}
			}
		}
	}

	auto GetFrequencyGroupCurrentFrequency = [&](int32 FrequencyGroupIdx)
	{
		for (int32 CoreIdx = 0; CoreIdx < NumCores; CoreIdx++)
		{
			if (CoreFrequencyGroupIndex[CoreIdx] == FrequencyGroupIdx)
			{
				uint32 CoreFreq = FAndroidMisc::GetCoreFrequency(CoreIdx, FAndroidMisc::ECoreFrequencyProperty::CurrentFrequency);
				if (CoreFreq > 0)
				{
					return ((float)CoreFreq / (float)FrequencyGroups[FrequencyGroupIdx].MaxFrequency) * 100.0f;
				}
			}
		}
		return 0.0f;
	};

	static float CurrentFrequencies[MaxFrequencyGroupStats] = { 0,0,0,0 };
	for (int32 FrequencyGroupIndex = 0; FrequencyGroupIndex < FrequencyGroups.Num(); FrequencyGroupIndex++)
	{
		if (bUpdateStats)
		{
			CurrentFrequencies[FrequencyGroupIndex] = GetFrequencyGroupCurrentFrequency(FrequencyGroupIndex);
		}
		CSV_CUSTOM_STAT_DEFINED_BY_PTR(GCPUFreqStats[FrequencyGroupIndex], CurrentFrequencies[FrequencyGroupIndex], ECsvCustomStatOp::Set);
	}

#if STATS
	static const FName AndroidFrequencyGroupMaxFreqStats[] = {
		GET_STATFNAME(STAT_FreqGroup0MaxFrequency),
		GET_STATFNAME(STAT_FreqGroup1MaxFrequency),
		GET_STATFNAME(STAT_FreqGroup2MaxFrequency),
		GET_STATFNAME(STAT_FreqGroup3MaxFrequency),
	};

	static const FName AndroidFrequencyGroupMinFreqStats[] = {
		GET_STATFNAME(STAT_FreqGroup0MinFrequency),
		GET_STATFNAME(STAT_FreqGroup1MinFrequency),
		GET_STATFNAME(STAT_FreqGroup2MinFrequency),
		GET_STATFNAME(STAT_FreqGroup3MinFrequency),
	};

	static const FName AndroidFrequencyGroupCurrentFreqStats[] = {
		GET_STATFNAME(STAT_FreqGroup0CurrentFrequency),
		GET_STATFNAME(STAT_FreqGroup1CurrentFrequency),
		GET_STATFNAME(STAT_FreqGroup2CurrentFrequency),
		GET_STATFNAME(STAT_FreqGroup3CurrentFrequency),
	};

	static const FName AndroidFrequencyGroupNumCoresStats[] = {
		GET_STATFNAME(STAT_FreqGroup0NumCores),
		GET_STATFNAME(STAT_FreqGroup1NumCores),
		GET_STATFNAME(STAT_FreqGroup2NumCores),
		GET_STATFNAME(STAT_FreqGroup3NumCores),
	};

	static const FName AndroidFrequencyGroupMaxCoresUtilizationStats[] = {
		GET_STATFNAME(STAT_FreqGroup0MaxUtilization),
		GET_STATFNAME(STAT_FreqGroup1MaxUtilization),
		GET_STATFNAME(STAT_FreqGroup2MaxUtilization),
		GET_STATFNAME(STAT_FreqGroup3MaxUtilization),
	};

	static float MaxSingleCoreUtilization[MaxFrequencyGroupStats] = { 0.0f };
	if (bUpdateStats)
	{
		FAndroidMisc::FCPUState& AndroidCPUState = FAndroidMisc::GetCPUState();
		for (int32 CoreIndex = 0; CoreIndex < NumCores; CoreIndex++)
		{
			uint32 FrequencyGroupIndex = CoreFrequencyGroupIndex[CoreIndex];
			if (FrequencyGroupIndex != 0xFFFFFFFF)
			{
				float& MaxCoreUtilization = MaxSingleCoreUtilization[FrequencyGroupIndex];
				MaxCoreUtilization = FMath::Max((float)AndroidCPUState.Utilization[CoreIndex], MaxCoreUtilization);
			}
		}
	}

	for (int32 FrequencyGroupIndex = 0; FrequencyGroupIndex < FrequencyGroups.Num(); FrequencyGroupIndex++)
	{
		const FFrequencyGroup& FrequencyGroup = FrequencyGroups[FrequencyGroupIndex];
		SET_DWORD_STAT_BY_FNAME(AndroidFrequencyGroupMaxFreqStats[FrequencyGroupIndex], FrequencyGroup.MaxFrequency);
		SET_DWORD_STAT_BY_FNAME(AndroidFrequencyGroupNumCoresStats[FrequencyGroupIndex], FrequencyGroup.CoreCount);
		//SET_DWORD_STAT_BY_FNAME(AndroidFrequencyGroupMinFreqStats[FrequencyGroupIndex], FrequencyGroup.MinFrequency);
		SET_FLOAT_STAT_BY_FNAME(AndroidFrequencyGroupCurrentFreqStats[FrequencyGroupIndex], CurrentFrequencies[FrequencyGroupIndex]);
		SET_FLOAT_STAT_BY_FNAME(AndroidFrequencyGroupMaxCoresUtilizationStats[FrequencyGroupIndex], MaxSingleCoreUtilization[FrequencyGroupIndex]);
	}

	static const FName CPUStatName = GET_STATFNAME(STAT_CPUTemp);
	static const FName ThermalStatus = GET_STATFNAME(STAT_ThermalStatus);
	SET_FLOAT_STAT_BY_FNAME(CPUStatName, CPUTemp);
	SET_FLOAT_STAT_BY_FNAME(ThermalStatus, GThermalStatus);
#endif
}
