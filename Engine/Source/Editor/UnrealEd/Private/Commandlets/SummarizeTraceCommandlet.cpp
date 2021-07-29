// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SummarizeTraceCommandlet.cpp: Commandlet for summarizing a utrace
=============================================================================*/

#include "Commandlets/SummarizeTraceCommandlet.h"

#include "Containers/StringConv.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "String/ParseTokens.h"
#include "Trace/Analysis.h"
#include "Trace/Analyzer.h"
#include "Trace/DataStream.h"
#include "TraceServices/Utils.h"
#include "ProfilingDebugging/CountersTrace.h"

/*
 * The following could be in TraceServices. This way if the format of CPU scope
 * events change this interface acts as a compatibility contract for external
 * tools. In the future it may be in a separate library so that Trace and
 * Insights' instrumentation are more broadly available.
 */

static uint64 Decode7bit(const uint8*& Cursor)
{
	uint64 Value = 0;
	uint64 ByteIndex = 0;
	bool bHasMoreBytes;
	do
	{
		uint8 ByteValue = *Cursor++;
		bHasMoreBytes = ByteValue & 0x80;
		Value |= uint64(ByteValue & 0x7f) << (ByteIndex * 7);
		++ByteIndex;
	} 	while (bHasMoreBytes);
	return Value;
}

class FCpuAnalyzer
	: public UE::Trace::IAnalyzer
{
public:
	struct FScopeName
	{
		const TCHAR*	Name;
		uint32			Id;
	};

	struct FScopeEnter
	{
		double			TimeStamp;
		uint32			ScopeId;
		uint32			ThreadId;
	};

	struct FScopeExit
	{
		double			TimeStamp;
		uint32			ThreadId;
	};

	virtual void		OnCpuScopeName(const FScopeName& ScopeName) = 0;
	virtual void		OnCpuScopeEnter(const FScopeEnter& ScopeEnter) = 0;
	virtual void		OnCpuScopeExit(const FScopeExit& ScopeExit) = 0;

private:
	virtual void		OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool		OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	void				OnEventSpec(const FOnEventContext& Context);
	void				OnBatch(const FOnEventContext& Context);
};

enum
{
	// CpuProfilerTrace.cpp
	RouteId_CpuProfiler_EventSpec,
	RouteId_CpuProfiler_EventBatch,
	RouteId_CpuProfiler_EndCapture,
};

void FCpuAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	Context.InterfaceBuilder.RouteEvent(RouteId_CpuProfiler_EventSpec, "CpuProfiler", "EventSpec");
	Context.InterfaceBuilder.RouteEvent(RouteId_CpuProfiler_EventBatch, "CpuProfiler", "EventBatch");
	Context.InterfaceBuilder.RouteEvent(RouteId_CpuProfiler_EndCapture, "CpuProfiler", "EndCapture");
}

bool FCpuAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	switch (RouteId)
	{
	case RouteId_CpuProfiler_EventSpec:
		OnEventSpec(Context);
		break;

	case RouteId_CpuProfiler_EventBatch:
	case RouteId_CpuProfiler_EndCapture:
		OnBatch(Context);
		break;
	};

	return true;
}

void FCpuAnalyzer::OnEventSpec(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	FString Name;
	uint32 Id = EventData.GetValue<uint32>("Id");
	EventData.GetString("Name", Name);
	OnCpuScopeName({ *Name, Id - 1 });
}

void FCpuAnalyzer::OnBatch(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	const FEventTime& EventTime = Context.EventTime;

	uint32 ThreadId = Context.ThreadInfo.GetId();

	TArrayView<const uint8> DataView = TraceServices::FTraceAnalyzerUtils::LegacyAttachmentArray("Data", Context);
	const uint8* Cursor = DataView.GetData();
	const uint8* End = Cursor + DataView.Num();
	uint64 LastCycle = 0;
	while (Cursor < End)
	{
		uint64 Value = Decode7bit(Cursor);
		uint64 Cycle = LastCycle + (Value >> 1);
		LastCycle = Cycle;

		double TimeStamp = EventTime.AsSeconds(Cycle);
		if (Value & 1)
		{
			uint64 ScopeId = Decode7bit(Cursor);
			OnCpuScopeEnter({ TimeStamp, uint32(ScopeId - 1), ThreadId });
		}
		else
		{
			OnCpuScopeExit({ TimeStamp, ThreadId });
		}
	}
}

class FCountersAnalyzer
	: public UE::Trace::IAnalyzer
{
public:
	struct FCounterName
	{
		const TCHAR*		Name;
		ETraceCounterType	Type;
		uint16				Id;
	};

	struct FCounterIntValue
	{
		uint16				Id;
		int64				Value;
	};

	struct FCounterFloatValue
	{
		uint16				Id;
		double				Value;
	};

	virtual void		OnCounterName(const FCounterName& CounterName) = 0;
	virtual void		OnCounterIntValue(const FCounterIntValue& NewValue) = 0;
	virtual void		OnCounterFloatValue(const FCounterFloatValue& NewValue) = 0;

private:
	virtual void		OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool		OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;
	void				OnCountersSpec(const FOnEventContext& Context);
	void				OnCountersSetValueInt(const FOnEventContext& Context);
	void				OnCountersSetValueFloat(const FOnEventContext& Context);
};

enum
{
	// CountersTrace.cpp
	RouteId_Counters_Spec,
	RouteId_Counters_SetValueInt,
	RouteId_Counters_SetValueFloat,
};

void FCountersAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	Context.InterfaceBuilder.RouteEvent(RouteId_Counters_Spec, "Counters", "Spec");
	Context.InterfaceBuilder.RouteEvent(RouteId_Counters_SetValueInt, "Counters", "SetValueInt");
	Context.InterfaceBuilder.RouteEvent(RouteId_Counters_SetValueFloat, "Counters", "SetValueFloat");
}

bool FCountersAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	switch (RouteId)
	{
	case RouteId_Counters_Spec:
		OnCountersSpec(Context);
		break;

	case RouteId_Counters_SetValueInt:
		OnCountersSetValueInt(Context);
		break;

	case RouteId_Counters_SetValueFloat:
		OnCountersSetValueFloat(Context);
		break;
	}

	return true;
}

void FCountersAnalyzer::OnCountersSpec(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	FString Name;
	uint16 Id = EventData.GetValue<uint16>("Id");
	ETraceCounterType Type = static_cast<ETraceCounterType>(EventData.GetValue<uint8>("Type"));
	EventData.GetString("Name", Name);
	OnCounterName({ *Name, Type, uint16(Id - 1) });
}

void FCountersAnalyzer::OnCountersSetValueInt(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	uint16 CounterId = EventData.GetValue<uint16>("CounterId");
	int64 Value = EventData.GetValue<int64>("Value");
	OnCounterIntValue({ uint16(CounterId - 1), Value });
}

void FCountersAnalyzer::OnCountersSetValueFloat(const FOnEventContext& Context)
{
	const FEventData& EventData = Context.EventData;
	uint16 CounterId = EventData.GetValue<uint16>("CounterId");
	double Value = EventData.GetValue<double>("Value");
	OnCounterFloatValue({ uint16(CounterId - 1), Value });
}

/*
 * This too could be housed elsewhere, along with an API to make it easier to
 * run analysis on trace files. The current model is influenced a little too much
 * on the store model that Insights' browser mode hosts.
 */

class FFileDataStream : public UE::Trace::IInDataStream
{
public:
	FFileDataStream()
		: Handle(nullptr)
	{
	}

	~FFileDataStream()
	{
		delete Handle;
	}

	bool Open(const TCHAR* Path)
	{
		Handle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(Path);
		if (Handle == nullptr)
		{
			return false;
		}
		Remaining = Handle->Size();
		return true;
	}

	virtual int32 Read(void* Data, uint32 Size) override
	{
		if (Remaining <= 0 || Handle == nullptr)
		{
			return 0;
		}

		Size = (Size < Remaining) ? Size : Remaining;
		Remaining -= Size;
		return Handle->Read((uint8*)Data, Size) ? Size : 0;
	}

	IFileHandle* Handle;
	uint64 Remaining;
};

/*
 * Helper classes for the SummarizeTrace commandlet. Aggregates statistics about a trace.
 */

class FSummarizeCpuAnalyzer
	: public FCpuAnalyzer
{
public:
	virtual void OnCpuScopeName(const FScopeName& ScopeName) override;
	virtual void OnCpuScopeEnter(const FScopeEnter& ScopeEnter) override;
	virtual void OnCpuScopeExit(const FScopeExit& ScopeExit) override;

	struct FThread
	{
		TArray<FScopeEnter> ScopeStack;
	};

	struct FScope
	{
		FString Name;
		uint64 Count = 0;
		double TotalDurationSeconds = 0.0;

		double FirstStartSeconds = 0.0;
		double FirstEndSeconds = 0.0;
		double FirstDurationSeconds = 0.0;

		double LastStartSeconds = 0.0;
		double LastEndSeconds = 0.0;
		double LastDurationSeconds = 0.0;

		double MinDurationSeconds = 1e10;
		double MaxDurationSeconds = -1e10;
		double MeanDurationSeconds = 0.0;
		double VarianceAcc = 0.0; // Accumulator for Welford's

		void AddDuration(double StartSeconds, double EndSeconds)
		{
			Count += 1;

			// compute the duration
			double DurationSeconds = EndSeconds - StartSeconds;

			// only set first for the first sample, compare exact zero
			if (FirstStartSeconds == 0.0)
			{
				FirstStartSeconds = StartSeconds;
				FirstEndSeconds = EndSeconds;
				FirstDurationSeconds = DurationSeconds;
			}

			LastStartSeconds = StartSeconds;
			LastEndSeconds = EndSeconds;
			LastDurationSeconds = DurationSeconds;

			// set duration statistics
			TotalDurationSeconds += DurationSeconds;
			MinDurationSeconds = FMath::Min(MinDurationSeconds, DurationSeconds);
			MaxDurationSeconds = FMath::Max(MaxDurationSeconds, DurationSeconds);
			UpdateVariance(DurationSeconds);
		}

		void UpdateVariance(double DurationSeconds)
		{
			ensure(Count);

			// Welford's increment
			double OldMeanDurationSeconds = MeanDurationSeconds;
			MeanDurationSeconds = MeanDurationSeconds + ((DurationSeconds - MeanDurationSeconds) / double(Count));
			VarianceAcc = VarianceAcc + ((DurationSeconds - MeanDurationSeconds) * (DurationSeconds - OldMeanDurationSeconds));
		}

		double GetDeviationDurationSeconds() const
		{
			if (Count > 1)
			{
				// Welford's final step, dependent on sample count
				double VarianceSecondsSquared = VarianceAcc / double(Count - 1);

				// stddev is sqrt of variance, to restore to units of seconds (vs. seconds squared)
				return sqrt(VarianceSecondsSquared);
			}
			else
			{
				return 0.0;
			}
		}

		void Merge(const FScope& Scope)
		{
			check(Name == Scope.Name);
			TotalDurationSeconds += Scope.TotalDurationSeconds;
			MinDurationSeconds = FMath::Min(MinDurationSeconds, Scope.MinDurationSeconds);
			MaxDurationSeconds = FMath::Max(MaxDurationSeconds, Scope.MaxDurationSeconds);
			Count += Scope.Count;
		}

		FString GetValue(const FStringView& Statistic) const
		{
			if (Statistic == TEXT("Name"))
			{
				return Name;
			}
			else if (Statistic == TEXT("Count"))
			{
				return FString::Printf(TEXT("%llu"), Count);
			}
			else if (Statistic == TEXT("TotalDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), TotalDurationSeconds);
			}
			else if (Statistic == TEXT("FirstStartSeconds"))
			{
				return FString::Printf(TEXT("%f"), FirstStartSeconds);
			}
			else if (Statistic == TEXT("FirstEndSeconds"))
			{
				return FString::Printf(TEXT("%f"), FirstEndSeconds);
			}
			else if (Statistic == TEXT("FirstDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), FirstDurationSeconds);
			}
			else if (Statistic == TEXT("LastStartSeconds"))
			{
				return FString::Printf(TEXT("%f"), LastStartSeconds);
			}
			else if (Statistic == TEXT("LastEndSeconds"))
			{
				return FString::Printf(TEXT("%f"), LastEndSeconds);
			}
			else if (Statistic == TEXT("LastDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), LastDurationSeconds);
			}
			else if (Statistic == TEXT("MinDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), MinDurationSeconds);
			}
			else if (Statistic == TEXT("MaxDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), MaxDurationSeconds);
			}
			else if (Statistic == TEXT("MeanDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), MeanDurationSeconds);
			}
			else if (Statistic == TEXT("DeviationDurationSeconds"))
			{
				return FString::Printf(TEXT("%f"), GetDeviationDurationSeconds());
			}
			return FString();
		}

		// for deduplication
		bool operator==(const FScope& Scope) const
		{
			return Name == Scope.Name;
		}

		// for sorting descending
		bool operator<(const FScope& Scope) const
		{
			return TotalDurationSeconds > Scope.TotalDurationSeconds;
		}
	};

	TArray<FScope> Scopes;
	TArray<FThread> Threads;
};

static uint32 GetTypeHash(const FSummarizeCpuAnalyzer::FScope Key)
{
	return FCrc::StrCrc32(*Key.Name);
}

void FSummarizeCpuAnalyzer::OnCpuScopeName(const FScopeName& ScopeName)
{
	if (ScopeName.Id >= uint32(Scopes.Num()))
	{
		uint32 Num = (ScopeName.Id + 128) & ~127;
		Scopes.SetNum(Num);
	}
	Scopes[ScopeName.Id].Name = ScopeName.Name;
}

void FSummarizeCpuAnalyzer::OnCpuScopeEnter(const FScopeEnter& ScopeEnter)
{
	uint32 ThreadId = ScopeEnter.ThreadId;
	if (ThreadId >= uint32(Threads.Num()))
	{
		Threads.SetNum(ThreadId + 1);
	}
	Threads[ThreadId].ScopeStack.Add(ScopeEnter);
}

void FSummarizeCpuAnalyzer::OnCpuScopeExit(const FScopeExit& ScopeExit)
{
	uint32 ThreadId = ScopeExit.ThreadId;
	if (ThreadId >= uint32(Threads.Num()) || Threads[ThreadId].ScopeStack.Num() <= 0)
	{
		return;
	}

	FScopeEnter ScopeEnter = Threads[ThreadId].ScopeStack.Pop();
	double ScopeDuration = ScopeExit.TimeStamp - ScopeEnter.TimeStamp;

	// unclear why we are getting ids that are out-of-bounds, fewer specs than scopes shouldn't be possible
	//  maybe we are losing spec data?
	//  maybe scope to spec id enc/dec has edge cases?
	if (ScopeEnter.ScopeId < uint32(Scopes.Num()))
	{
		Scopes[ScopeEnter.ScopeId].AddDuration(ScopeEnter.TimeStamp, ScopeExit.TimeStamp);
	}
}

class FSummarizeCountersAnalyzer
	: public FCountersAnalyzer
{
public:
	virtual void OnCounterName(const FCounterName& CounterName) override;
	virtual void OnCounterIntValue(const FCounterIntValue& NewValue) override;
	virtual void OnCounterFloatValue(const FCounterFloatValue& NewValue) override;

	struct FCounter
	{
		FString Name;
		ETraceCounterType Type;

		union
		{
			int64 IntValue;
			double FloatValue;
		};

		FCounter(FString InName, ETraceCounterType InType)
		{
			Name = InName;
			Type = InType;
			switch (Type)
			{
			case TraceCounterType_Int:
				IntValue = 0;
				break;

			case TraceCounterType_Float:
				FloatValue = 0.0;
				break;
			}
		}

		template<typename ValueType>
		void SetValue(ValueType InValue) = delete;

		template<>
		void SetValue(int64 InValue)
		{
			ensure(Type == TraceCounterType_Int);
			if (Type == TraceCounterType_Int)
			{
				IntValue = InValue;
			}
		}

		template<>
		void SetValue(double InValue)
		{
			ensure(Type == TraceCounterType_Float);
			if (Type == TraceCounterType_Float)
			{
				FloatValue = InValue;
			}
		}

		FString GetValue() const
		{
			switch (Type)
			{
			case TraceCounterType_Int:
				return FString::Printf(TEXT("%lld"), IntValue);

			case TraceCounterType_Float:
				return FString::Printf(TEXT("%f"), FloatValue);
			}

			ensure(false);
			return TEXT("");
		}
	};

	TMap<uint16, FCounter> Counters;
};

void FSummarizeCountersAnalyzer::OnCounterName(const FCounterName& CounterName)
{
	Counters.Add(CounterName.Id, FCounter(CounterName.Name, CounterName.Type));
}

void FSummarizeCountersAnalyzer::OnCounterIntValue(const FCounterIntValue& NewValue)
{
	FCounter* FoundCounter = Counters.Find(NewValue.Id);
	ensure(FoundCounter);
	if (FoundCounter)
	{
		FoundCounter->SetValue(NewValue.Value);
	}
}

void FSummarizeCountersAnalyzer::OnCounterFloatValue(const FCounterFloatValue& NewValue)
{
	FCounter* FoundCounter = Counters.Find(NewValue.Id);
	ensure(FoundCounter);
	if (FoundCounter)
	{
		FoundCounter->SetValue(NewValue.Value);
	}
}

/*
* Helper class for the stats csv file
*/

struct StatisticDefinition
{
	StatisticDefinition()
	{}

	StatisticDefinition(const FString& InName, const FString& InStatistic,
		const FString& InTelemetryContext, const FString& InTelemetryDataPoint, const FString& InTelemetryUnit,
		const FString& InBaselineWarningThreshold, const FString& InBaselineErrorThreshold)
		: Name(InName)
		, Statistic(InStatistic)
		, TelemetryContext(InTelemetryContext)
		, TelemetryDataPoint(InTelemetryDataPoint)
		, TelemetryUnit(InTelemetryUnit)
		, BaselineWarningThreshold(InBaselineWarningThreshold)
		, BaselineErrorThreshold(InBaselineErrorThreshold)
	{}

	StatisticDefinition(const StatisticDefinition& InStatistic)
		: Name(InStatistic.Name)
		, Statistic(InStatistic.Statistic)
		, TelemetryContext(InStatistic.TelemetryContext)
		, TelemetryDataPoint(InStatistic.TelemetryDataPoint)
		, TelemetryUnit(InStatistic.TelemetryUnit)
		, BaselineWarningThreshold(InStatistic.BaselineWarningThreshold)
		, BaselineErrorThreshold(InStatistic.BaselineErrorThreshold)
	{}

	bool operator==(const StatisticDefinition& InStatistic) const
	{
		return Name == InStatistic.Name
			&& Statistic == InStatistic.Statistic
			&& TelemetryContext == InStatistic.TelemetryContext
			&& TelemetryDataPoint == InStatistic.TelemetryDataPoint
			&& TelemetryUnit == InStatistic.TelemetryUnit
			&& BaselineWarningThreshold == InStatistic.BaselineWarningThreshold
			&& BaselineErrorThreshold == InStatistic.BaselineErrorThreshold;
	}

	static bool LoadFromCSV(const FString& FilePath, TMultiMap<FString, StatisticDefinition>& NameToDefinitionMap);

	FString Name;
	FString Statistic;
	FString TelemetryContext;
	FString TelemetryDataPoint;
	FString TelemetryUnit;
	FString BaselineWarningThreshold;
	FString BaselineErrorThreshold;
};

bool StatisticDefinition::LoadFromCSV(const FString& FilePath, TMultiMap<FString, StatisticDefinition>& NameToDefinitionMap)
{
	TArray<FString> ParsedCSVFile;
	FFileHelper::LoadFileToStringArray(ParsedCSVFile, *FilePath);

	int NameColumn = -1;
	int StatisticColumn = -1;
	int TelemetryContextColumn = -1;
	int TelemetryDataPointColumn = -1;
	int TelemetryUnitColumn = -1;
	int BaselineWarningThresholdColumn = -1;
	int BaselineErrorThresholdColumn = -1;
	struct Column
	{
		const TCHAR* Name = nullptr;
		int* Index = nullptr;
	}
	Columns[] =
	{
		{ TEXT("Name"), &NameColumn },
		{ TEXT("Statistic"), &StatisticColumn },
		{ TEXT("TelemetryContext"), &TelemetryContextColumn },
		{ TEXT("TelemetryDataPoint"), &TelemetryDataPointColumn },
		{ TEXT("TelemetryUnit"), &TelemetryUnitColumn },
		{ TEXT("BaselineWarningThreshold"), &BaselineWarningThresholdColumn },
		{ TEXT("BaselineErrorThreshold"), &BaselineErrorThresholdColumn },
	};

	bool bValidColumns = true;
	for (int CSVIndex = 0; CSVIndex < ParsedCSVFile.Num() && bValidColumns; ++CSVIndex)
	{
		const FString& CSVEntry = ParsedCSVFile[CSVIndex];
		TArray<FString> Fields;
		UE::String::ParseTokens(CSVEntry.TrimStartAndEnd(), TEXT(','),
			[&Fields](FStringView Field)
			{
				Fields.Add(FString(Field));
			});

		if (CSVIndex == 0) // is this the header row?
		{
			for (struct Column& Column : Columns)
			{
				for (int FieldIndex = 0; FieldIndex < Fields.Num(); ++FieldIndex)
				{
					if (Fields[FieldIndex] == Column.Name)
					{
						(*Column.Index) = FieldIndex;
						break;
					}
				}

				if (*Column.Index == -1)
				{
					bValidColumns = false;
				}
			}
		}
		else // else it is a data row, pull each element from appropriate column
		{
			const FString& Name(Fields[NameColumn]);
			const FString& Statistic(Fields[StatisticColumn]);
			const FString& TelemetryContext(Fields[TelemetryContextColumn]);
			const FString& TelemetryDataPoint(Fields[TelemetryDataPointColumn]);
			const FString& TelemetryUnit(Fields[TelemetryUnitColumn]);
			const FString& BaselineWarningThreshold(Fields[BaselineWarningThresholdColumn]);
			const FString& BaselineErrorThreshold(Fields[BaselineErrorThresholdColumn]);
			NameToDefinitionMap.AddUnique(Name, StatisticDefinition(Name, Statistic, TelemetryContext, TelemetryDataPoint, TelemetryUnit, BaselineWarningThreshold, BaselineErrorThreshold));
		}
	}

	return bValidColumns;
}

/*
* Helper class for the telemetry csv file
*/

struct TelemetryDefinition
{
	TelemetryDefinition()
	{}

	TelemetryDefinition(const FString& InTestName, const FString& InContext, const FString& InDataPoint, const FString& InUnit,
		const FString& InMeasurement, const FString* InBaseline = nullptr)
		: TestName(InTestName)
		, Context(InContext)
		, DataPoint(InDataPoint)
		, Unit(InUnit)
		, Measurement(InMeasurement)
		, Baseline(InBaseline ? *InBaseline : FString ())
	{}

	TelemetryDefinition(const TelemetryDefinition& InStatistic)
		: TestName(InStatistic.TestName)
		, Context(InStatistic.Context)
		, DataPoint(InStatistic.DataPoint)
		, Unit(InStatistic.Unit)
		, Measurement(InStatistic.Measurement)
		, Baseline(InStatistic.Baseline)
	{}

	bool operator==(const TelemetryDefinition& InStatistic) const
	{
		return TestName == InStatistic.TestName
			&& Context == InStatistic.Context
			&& DataPoint == InStatistic.DataPoint
			&& Measurement == InStatistic.Measurement
			&& Baseline == InStatistic.Baseline
			&& Unit == InStatistic.Unit;
	}

	static bool LoadFromCSV(const FString& FilePath, TMap<TPair<FString,FString>, TelemetryDefinition>& ContextAndDataPointToDefinitionMap);
	static bool MeasurementWithinThreshold(const FString& Value, const FString& BaselineValue, const FString& Threshold);
	static FString SignFlipThreshold(const FString& Threshold);

	FString TestName;
	FString Context;
	FString DataPoint;
	FString Unit;
	FString Measurement;
	FString Baseline;
};

bool TelemetryDefinition::LoadFromCSV(const FString& FilePath, TMap<TPair<FString, FString>, TelemetryDefinition>& ContextAndDataPointToDefinitionMap)
{
	TArray<FString> ParsedCSVFile;
	FFileHelper::LoadFileToStringArray(ParsedCSVFile, *FilePath);

	int TestNameColumn = -1;
	int ContextColumn = -1;
	int DataPointColumn = -1;
	int UnitColumn = -1;
	int MeasurementColumn = -1;
	int BaselineColumn = -1;
	struct Column
	{
		const TCHAR* Name = nullptr;
		int* Index = nullptr;
		bool bRequired = true;
	}
	Columns[] =
	{
		{ TEXT("TestName"), &TestNameColumn },
		{ TEXT("Context"), &ContextColumn },
		{ TEXT("DataPoint"), &DataPointColumn },
		{ TEXT("Unit"), &UnitColumn },
		{ TEXT("Measurement"), &MeasurementColumn },
		{ TEXT("Baseline"), &BaselineColumn, false },
	};

	bool bValidColumns = true;
	for (int CSVIndex = 0; CSVIndex < ParsedCSVFile.Num() && bValidColumns; ++CSVIndex)
	{
		const FString& CSVEntry = ParsedCSVFile[CSVIndex];
		TArray<FString> Fields;
		UE::String::ParseTokens(CSVEntry.TrimStartAndEnd(), TEXT(','),
			[&Fields](FStringView Field)
			{
				Fields.Add(FString(Field));
			});

		if (CSVIndex == 0) // is this the header row?
		{
			for (struct Column& Column : Columns)
			{
				for (int FieldIndex = 0; FieldIndex < Fields.Num(); ++FieldIndex)
				{
					if (Fields[FieldIndex] == Column.Name)
					{
						(*Column.Index) = FieldIndex;
						break;
					}
				}

				if (*Column.Index == -1 && Column.bRequired)
				{
					bValidColumns = false;
				}
			}
		}
		else // else it is a data row, pull each element from appropriate column
		{
			const FString& TestName(Fields[TestNameColumn]);
			const FString& Context(Fields[ContextColumn]);
			const FString& DataPoint(Fields[DataPointColumn]);
			const FString& Unit(Fields[UnitColumn]);
			const FString& Measurement(Fields[MeasurementColumn]);

			FString Baseline;
			if (BaselineColumn != -1)
			{
				Baseline = Fields[BaselineColumn];
			}

			ContextAndDataPointToDefinitionMap.Add(TPair<FString, FString>(Context, DataPoint), TelemetryDefinition(TestName, Context, DataPoint, Unit, Measurement, &Baseline));
		}
	}

	return bValidColumns;
}

bool TelemetryDefinition::MeasurementWithinThreshold(const FString& MeasurementValue, const FString& BaselineValue, const FString& Threshold)
{
	if (Threshold.IsEmpty())
	{
		return true;
	}

	// detect threshold as delta percentage
	int32 PercentIndex = INDEX_NONE;
	if (Threshold.FindChar(TEXT('%'), PercentIndex))
	{
		FString ThresholdWithoutPercentSign = Threshold;
		ThresholdWithoutPercentSign.RemoveAt(PercentIndex);

		double Factor = 1.0 + (FCString::Atod(*ThresholdWithoutPercentSign) / 100.0);
		double RationalValue = FCString::Atod(*MeasurementValue);
		double RationalBaselineValue = FCString::Atod(*BaselineValue);
		if (Factor >= 1.0)
		{
			return RationalValue < (RationalBaselineValue * Factor);
		}
		else
		{
			return RationalValue > (RationalBaselineValue * Factor);
		}
	}
	else // threshold as delta cardinal value
	{
		// rational number, use float math
		if (Threshold.Contains(TEXT(".")))
		{
			double Delta = FCString::Atod(*Threshold);
			double RationalValue = FCString::Atod(*MeasurementValue);
			double RationalBaselineValue = FCString::Atod(*BaselineValue);
			if (Delta > 0.0)
			{
				return RationalValue <= (RationalBaselineValue + Delta);
			}
			else if (Delta < 0.0)
			{
				return RationalValue >= (RationalBaselineValue + Delta);
			}
			else
			{
				return fabs(RationalBaselineValue - RationalValue) < FLT_EPSILON;
			}
		}
		else // natural number, use int math
		{
			int64 Delta = FCString::Strtoi64(*Threshold, nullptr, 10);
			int64 NaturalValue = FCString::Strtoi64(*MeasurementValue, nullptr, 10);
			int64 NaturalBaselineValue = FCString::Strtoi64(*BaselineValue, nullptr, 10);
			if (Delta > 0)
			{
				return NaturalValue <= (NaturalBaselineValue + Delta);
			}
			else if (Delta < 0)
			{
				return NaturalValue >= (NaturalBaselineValue + Delta);
			}
			else
			{
				return NaturalValue == NaturalBaselineValue;
			}
		}
	}
}

FString TelemetryDefinition::SignFlipThreshold(const FString& Threshold)
{
	FString SignFlipped;

	if (Threshold.StartsWith(TEXT("-")))
	{
		SignFlipped = Threshold.RightChop(1);
	}
	else
	{
		SignFlipped = FString(TEXT("-")) + Threshold;
	}

	return SignFlipped;
}

/*
 * SummarizeTrace commandlet ingests a utrace file and summarizes the
 * cpu scope events within it, and summarizes each event to a csv. It
 * also can generate a telemetry file given statistics csv about what
 * events and what statistics you would like to track.
 */

DEFINE_LOG_CATEGORY_STATIC(LogSummarizeTrace, Log, All);

USummarizeTraceCommandlet::USummarizeTraceCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 USummarizeTraceCommandlet::Main(const FString& CmdLineParams)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamVals;
	UCommandlet::ParseCommandLine(*CmdLineParams, Tokens, Switches, ParamVals);

	// Display help
	if (Switches.Contains("help"))
	{
		UE_LOG(LogSummarizeTrace, Log, TEXT("SummarizeTrace"));
		UE_LOG(LogSummarizeTrace, Log, TEXT("This commandlet will summarize a utrace into something more easily ingestable by a reporting tool (csv)."));
		UE_LOG(LogSummarizeTrace, Log, TEXT("Options:"));
		UE_LOG(LogSummarizeTrace, Log, TEXT(" Required: -inputfile=<utrace path>   (The utrace you wish to process)"));
		UE_LOG(LogSummarizeTrace, Log, TEXT(" Optional: -statsfile=<csv path>      (The csv of statistics to generate)"));
		UE_LOG(LogSummarizeTrace, Log, TEXT(" Optional: -testname=<string>         (Test name to use in telemetry csv)"));
		return 0;
	}

	FString TraceFileName;
	if (FParse::Value(*CmdLineParams, TEXT("inputfile="), TraceFileName, true))
	{
		UE_LOG(LogSummarizeTrace, Display, TEXT("Loading trace from %s"), *TraceFileName);
	}
	else
	{
		UE_LOG(LogSummarizeTrace, Error, TEXT("You must specify a utrace file using -inputfile=<path>"));
		return 1;
	}

	// load the stats file to know which event name and statistic name to generate in the telementry csv
	// the telemetry csv is ingested completely, so this just whitelists specific data elements we want to track
	FString StatisticsFileName;
	TMultiMap<FString, StatisticDefinition> NameToDefinitionMap;
	if (FParse::Value(*CmdLineParams, TEXT("statsfile="), StatisticsFileName, true))
	{
		UE_LOG(LogSummarizeTrace, Display, TEXT("Loading statistics from %s"), *StatisticsFileName);
		bool bCSVOk = StatisticDefinition::LoadFromCSV(StatisticsFileName, NameToDefinitionMap);
		check(bCSVOk);
	}

	bool bFound;
	if (FPaths::FileExists(TraceFileName))
	{
		bFound = true;
	}
	else
	{
		bFound = false;
		TArray<FString> SearchPaths;
		SearchPaths.Add(FPaths::Combine(FPaths::EngineDir(), TEXT("Programs"), TEXT("UnrealInsights"), TEXT("Saved"), TEXT("TraceSessions")));
		SearchPaths.Add(FPaths::EngineDir());
		SearchPaths.Add(FPaths::ProjectDir());
		for (const FString& SearchPath : SearchPaths)
		{
			FString PossibleTraceFileName = FPaths::Combine(SearchPath, TraceFileName);
			if (FPaths::FileExists(PossibleTraceFileName))
			{
				TraceFileName = PossibleTraceFileName;
				bFound = true;
				break;
			}
		}
	}

	if (!bFound)
	{
		UE_LOG(LogSummarizeTrace, Error, TEXT("Trace file '%s' was not found"), *TraceFileName);
		return 1;
	}

	FFileDataStream DataStream;
	if (!DataStream.Open(*TraceFileName))
	{
		UE_LOG(LogSummarizeTrace, Error, TEXT("Unable to open trace file '%s' for read"), *TraceFileName);
		return 1;
	}

	// setup analysis context with analyzers
	UE::Trace::FAnalysisContext AnalysisContext;
	FSummarizeCpuAnalyzer CpuAnalyzer;
	AnalysisContext.AddAnalyzer(CpuAnalyzer);
	FSummarizeCountersAnalyzer CountersAnalyzer;
	AnalysisContext.AddAnalyzer(CountersAnalyzer);

	// kick processing on a thread
	UE::Trace::FAnalysisProcessor AnalysisProcessor = AnalysisContext.Process(DataStream);

	// sync on completion
	AnalysisProcessor.Wait();

	TSet<FSummarizeCpuAnalyzer::FScope> DeduplicatedScopes;

	struct DisallowedString
	{
		const TCHAR* String;
		bool First;
	}
	DisallowedStrings[] =
	{
		// breaks simple csv files
		{ TEXT("\n"), true },
		{ TEXT("\r"), true },
		{ TEXT(","), true },

		// likely a path to an asset
		//  this is broad but unfortunate with current instrumentation
		//  at the current moment names can start with /Relpath/To/Asset
		{ TEXT("/"), true }, 
	};

	bool bDisallowed = false;
	for (const FSummarizeCpuAnalyzer::FScope& Scope : CpuAnalyzer.Scopes)
	{
		if (Scope.Name.IsEmpty())
		{
			continue;
		}

		if (Scope.Count == 0)
		{
			continue;
		}

		// sanitize strings for a bog-simple csv file
		bDisallowed = false;
		for (struct DisallowedString& DisallowedString : DisallowedStrings)
		{
			if (Scope.Name.Contains(DisallowedString.String))
			{
				if (DisallowedString.First)
				{
					UE_LOG(LogSummarizeTrace, Display, TEXT("A scope contains disallowed string '%s'. See log for full list."), DisallowedString.String);
					DisallowedString.First = false;
				}

				UE_LOG(LogSummarizeTrace, Verbose, TEXT("Scope '%s' contains disallowed string '%s', skipping..."), *Scope.Name, DisallowedString.String);
				bDisallowed = true;
			}

			if (bDisallowed)
			{
				break;
			}
		}

		if (bDisallowed)
		{
			continue;
		}

		FSummarizeCpuAnalyzer::FScope* FoundScope = DeduplicatedScopes.Find(Scope);
		if (FoundScope)
		{
			FoundScope->Merge(Scope);
		}
		else
		{
			DeduplicatedScopes.Add(Scope);
		}
	}

	UE_LOG(LogSummarizeTrace, Display, TEXT("Sorting %d events by total time accumulated..."), DeduplicatedScopes.Num());
	TArray<FSummarizeCpuAnalyzer::FScope> SortedScopes;
	for (const FSummarizeCpuAnalyzer::FScope& Scope : DeduplicatedScopes)
	{
		SortedScopes.Add(Scope);
	}
	SortedScopes.Sort();

	// csv is UTF-8, so encode every string we print
	auto WriteUTF8 = [](IFileHandle* Handle, const FString& String)
	{
		const auto& UTF8String = StringCast<ANSICHAR>(*String);
		Handle->Write(reinterpret_cast<const uint8*>(UTF8String.Get()), UTF8String.Length());
	};

	// generate a summary csv, always
	FString CsvFileName = FPaths::SetExtension(TraceFileName, "csv");
	UE_LOG(LogSummarizeTrace, Display, TEXT("Writing summary to %s..."), *CsvFileName);
	IFileHandle* CsvHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*CsvFileName);
	if (CsvHandle)
	{
		// no newline, see row printfs
		WriteUTF8(CsvHandle, FString::Printf(TEXT("Name,Count,TotalDurationSeconds,FirstStartSeconds,FirstEndSeconds,FirstDurationSeconds,LastStartSeconds,LastEndSeconds,LastDurationSeconds,MinDurationSeconds,MaxDurationSeconds,MeanDurationSeconds,DeviationDurationSeconds,")));
		for (const FSummarizeCpuAnalyzer::FScope& Scope : SortedScopes)
		{
			// note newline is at the front of every data line to prevent final extraneous newline, per customary for csv
			WriteUTF8(CsvHandle, FString::Printf(TEXT("\n%s,%llu,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,"), *Scope.Name, Scope.Count, Scope.TotalDurationSeconds, Scope.FirstStartSeconds, Scope.FirstEndSeconds, Scope.FirstDurationSeconds, Scope.FirstStartSeconds, Scope.FirstEndSeconds, Scope.FirstDurationSeconds, Scope.MinDurationSeconds, Scope.MaxDurationSeconds, Scope.MeanDurationSeconds, Scope.GetDeviationDurationSeconds()));
		}
		for (const TMap<uint16, FSummarizeCountersAnalyzer::FCounter>::ElementType& Counter : CountersAnalyzer.Counters)
		{
			// note newline is at the front of every data line to prevent final extraneous newline, per customary for csv
			WriteUTF8(CsvHandle, FString::Printf(TEXT("\n%s,%s,,,,,,,,,,,,"), *Counter.Value.Name, *Counter.Value.GetValue()));
		}
		CsvHandle->Flush();
		delete CsvHandle;
		CsvHandle = nullptr;
	}
	else
	{
		UE_LOG(LogSummarizeTrace, Error, TEXT("Unable to open csv '%s' for write"), *CsvFileName);
		return 1;
	}

	// if we were asked to generate a telememtry file, generate it
	if (!NameToDefinitionMap.IsEmpty())
	{
		FString TracePath = FPaths::GetPath(TraceFileName);
		FString TraceFileBasename = FPaths::GetBaseFilename(TraceFileName);
		FString TelemetryCsvFileName = TraceFileBasename + TEXT("Telemetry");
		TelemetryCsvFileName = FPaths::Combine(TracePath, FPaths::SetExtension(TelemetryCsvFileName, "csv"));

		// override the test name
		FString TestName = TraceFileBasename;
		FParse::Value(*CmdLineParams, TEXT("testname="), TestName, true);

		TArray<TelemetryDefinition> TelemetryData;

		// resolve scopes to telemetry
		for (const FSummarizeCpuAnalyzer::FScope& Scope : SortedScopes)
		{
			TArray<StatisticDefinition> Statistics;
			NameToDefinitionMap.MultiFind(Scope.Name, Statistics, true);
			for (const StatisticDefinition& Statistic : Statistics)
			{
				TelemetryData.Add(TelemetryDefinition(TestName, Statistic.TelemetryContext, Statistic.TelemetryDataPoint, Statistic.TelemetryUnit, Scope.GetValue(Statistic.Statistic)));
			}
		}

		// resolve counters to telemetry
		for (const TMap<uint16, FSummarizeCountersAnalyzer::FCounter>::ElementType& Counter : CountersAnalyzer.Counters)
		{
			TArray<StatisticDefinition> Statistics;
			NameToDefinitionMap.MultiFind(Counter.Value.Name, Statistics, true);
			ensure(Statistics.Num() <= 1); // there should only be one, the counter value
			for (const StatisticDefinition& Statistic : Statistics)
			{
				TelemetryData.Add(TelemetryDefinition(TestName, Statistic.TelemetryContext, Statistic.TelemetryDataPoint, Statistic.TelemetryUnit, Counter.Value.GetValue()));
			}
		}

		// compare vs. baseline telemetry file, if it exists
		// note this does assume that the tracefile basename is directly comparable to a file in the baseline folder
		FString BaselineTelemetryCsvFilePath = FPaths::Combine(FPaths::EngineDir(), TEXT("Build"), TEXT("Baseline"), FPaths::SetExtension(TraceFileBasename + TEXT("Telemetry"), "csv"));
		if (FParse::Param(*CmdLineParams, TEXT("skipbaseline")))
		{
			BaselineTelemetryCsvFilePath.Empty();
		}
		if (FPaths::FileExists(BaselineTelemetryCsvFilePath))
		{
			UE_LOG(LogSummarizeTrace, Display, TEXT("Comparing telemetry to baseline telemetry %s..."), *TelemetryCsvFileName);

			// each context (scope name or coutner name) and data point (statistic name) pair form a key, an item to check
			TMap<TPair<FString, FString>, TelemetryDefinition> ContextAndDataPointToDefinitionMap;
			bool bCSVOk = TelemetryDefinition::LoadFromCSV(*BaselineTelemetryCsvFilePath, ContextAndDataPointToDefinitionMap);
			check(bCSVOk);

			// for every telemetry item we wrote for this trace...
			for (TelemetryDefinition& Telemetry : TelemetryData)
			{
				// the threshold is defined along with the original statistic map
				const StatisticDefinition* RelatedStatistic = nullptr;

				// find the statistic definition
				TArray<StatisticDefinition> Statistics;
				NameToDefinitionMap.MultiFind(Telemetry.Context, Statistics, true);
				for (const StatisticDefinition& Statistic : Statistics)
				{
					// the find will match on name, here we just need to find the right statistic for that named item
					if (Statistic.Statistic == Telemetry.DataPoint)
					{
						// we found it!
						RelatedStatistic = &Statistic;
						break;
					}
				}

				// do we still have the statistic definition in our current stats file? (if we don't that's fine, we don't care about it anymore)
				if (RelatedStatistic)
				{
					// find the corresponding keyed telemetry item in the baseline telemetry file...
					TelemetryDefinition* BaselineTelemetry = ContextAndDataPointToDefinitionMap.Find(TPair<FString, FString>(Telemetry.Context, Telemetry.DataPoint));
					if (BaselineTelemetry)
					{
						Telemetry.Baseline = BaselineTelemetry->Measurement;

						// let's only report on statistics that have an assigned threshold, to keep things concise
						if (!RelatedStatistic->BaselineWarningThreshold.IsEmpty() || !RelatedStatistic->BaselineErrorThreshold.IsEmpty())
						{
							// verify that this telemetry measurement is within the allowed threshold as defined in the current stats file
							if (TelemetryDefinition::MeasurementWithinThreshold(Telemetry.Measurement, BaselineTelemetry->Measurement, RelatedStatistic->BaselineWarningThreshold))
							{
								FString SignFlippedWarningThreshold = TelemetryDefinition::SignFlipThreshold(RelatedStatistic->BaselineWarningThreshold);

								// check if it's beyond the threshold the other way and needs lowering in the stats csv
								if (!TelemetryDefinition::MeasurementWithinThreshold(Telemetry.Measurement, BaselineTelemetry->Measurement, SignFlippedWarningThreshold))
								{
									FString BaselineRelPath = FPaths::ConvertRelativePathToFull(BaselineTelemetryCsvFilePath);
									FPaths::MakePathRelativeTo(BaselineRelPath, *FPaths::RootDir());

									FString StatisticsRelPath = FPaths::ConvertRelativePathToFull(StatisticsFileName);
									FPaths::MakePathRelativeTo(StatisticsRelPath, *FPaths::RootDir());

									UE_LOG(LogSummarizeTrace, Warning, TEXT("Telemetry %s,%s,%s,%s significantly within baseline value %s using warning threshold %s. Please submit a new baseline to %s or adjust the threshold in %s."),
										*Telemetry.TestName, *Telemetry.Context, *Telemetry.DataPoint, *Telemetry.Measurement,
										*BaselineTelemetry->Measurement, *RelatedStatistic->BaselineWarningThreshold,
										*BaselineRelPath, *StatisticsRelPath);
								}
								else // it's within tolerance, just report that it's ok
								{
									UE_LOG(LogSummarizeTrace, Verbose, TEXT("Telemetry %s,%s,%s,%s within baseline value %s using warning threshold %s"),
										*Telemetry.TestName, *Telemetry.Context, *Telemetry.DataPoint, *Telemetry.Measurement,
										*BaselineTelemetry->Measurement, *RelatedStatistic->BaselineWarningThreshold);
								}
							}
							else
							{
								// it's outside warning threshold, check if it's inside the error threshold to just issue a warning
								if (TelemetryDefinition::MeasurementWithinThreshold(Telemetry.Measurement, BaselineTelemetry->Measurement, RelatedStatistic->BaselineErrorThreshold))
								{
									UE_LOG(LogSummarizeTrace, Warning, TEXT("Telemetry %s,%s,%s,%s beyond baseline value %s using warning threshold %s. This could be a performance regression!"),
										*Telemetry.TestName, *Telemetry.Context, *Telemetry.DataPoint, *Telemetry.Measurement,
										*BaselineTelemetry->Measurement, *RelatedStatistic->BaselineWarningThreshold);
								}
								else // it's outside the error threshold, hard error
								{
									UE_LOG(LogSummarizeTrace, Error, TEXT("Telemetry %s,%s,%s,%s beyond baseline value %s using error threshold %s. This could be a performance regression!"),
										*Telemetry.TestName, *Telemetry.Context, *Telemetry.DataPoint, *Telemetry.Measurement,
										*BaselineTelemetry->Measurement, *RelatedStatistic->BaselineErrorThreshold);
								}
							}
						}
					}
					else
					{
						UE_LOG(LogSummarizeTrace, Display, TEXT("Telemetry for %s,%s has no baseline measurement, skipping..."), *Telemetry.Context, *Telemetry.DataPoint);
					}
				}
			}
		}

		UE_LOG(LogSummarizeTrace, Display, TEXT("Writing telemetry to %s..."), *TelemetryCsvFileName);
		IFileHandle* TelemetryCsvHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*TelemetryCsvFileName);
		if (TelemetryCsvHandle)
		{
			// no newline, see row printfs
			WriteUTF8(TelemetryCsvHandle, FString::Printf(TEXT("TestName,Context,DataPoint,Unit,Measurement,Baseline,")));
			for (const TelemetryDefinition& Telemetry : TelemetryData)
			{
				// note newline is at the front of every data line to prevent final extraneous newline, per customary for csv
				WriteUTF8(TelemetryCsvHandle, FString::Printf(TEXT("\n%s,%s,%s,%s,%s,%s,"), *Telemetry.TestName, *Telemetry.Context, *Telemetry.DataPoint, *Telemetry.Unit, *Telemetry.Measurement, *Telemetry.Baseline));
			}

			TelemetryCsvHandle->Flush();
			delete TelemetryCsvHandle;
			TelemetryCsvHandle = nullptr;
		}
		else
		{
			UE_LOG(LogSummarizeTrace, Error, TEXT("Unable to open telemetry csv '%s' for write"), *TelemetryCsvFileName);
			return 1;
		}
	}

	return 0;
}
