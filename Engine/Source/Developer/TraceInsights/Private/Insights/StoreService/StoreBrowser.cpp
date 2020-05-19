// Copyright Epic Games, Inc. All Rights Reserved.

#include "StoreBrowser.h"

#include "Trace/Analysis.h"
#include "Trace/StoreClient.h"

// Insights
#include "Insights/Common/Stopwatch.h"
#include "Insights/InsightsManager.h"
#include "Insights/Log.h"
#include "Insights/StoreService/DiagnosticsSessionAnalyzer.h"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FStoreBrowser::FStoreBrowser()
	: bRunning(true)
	, Thread(nullptr)
	, TracesCriticalSection()
	, bTracesLocked(false)
	, StoreChangeSerial(0)
	, TracesChangeSerial(0)
	, Traces()
	, TraceMap()
	, LiveTraceMap()
{
	Thread = FRunnableThread::Create(this, TEXT("StoreBrowser"));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FStoreBrowser::~FStoreBrowser()
{
	Stop();

	if (Thread)
	{
		Thread->Kill(true);
		Thread = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FStoreBrowser::Init()
{
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

uint32 FStoreBrowser::Run()
{
	while (bRunning)
	{
		FPlatformProcess::SleepNoStats(0.5f);
		UpdateTraces();
	}
	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStoreBrowser::Stop()
{
	bRunning = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStoreBrowser::Exit()
{
	ResetTraces();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

Trace::FStoreClient* FStoreBrowser::GetStoreClient() const
{
	// TODO: thread safety !?
	return FInsightsManager::Get()->GetStoreClient();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStoreBrowser::UpdateTraces()
{
	Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		ResetTraces();
		return;
	}

	FStopwatch StopwatchTotal;
	StopwatchTotal.Start();

	// Check if the list of trace sessions has changed.
	{
		const Trace::FStoreClient::FStatus* Status = StoreClient->GetStatus();
		if (StoreChangeSerial != Status->GetChangeSerial())
		{
			StoreChangeSerial = Status->GetChangeSerial();

			// Check for removed traces.
			{
				for (int32 TraceIndex = Traces.Num() - 1; TraceIndex >= 0; --TraceIndex)
				{
					const uint32 TraceId = Traces[TraceIndex]->TraceId;
					const Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(TraceId);
					if (TraceInfo == nullptr)
					{
						FScopeLock Lock(&TracesCriticalSection);
						TracesChangeSerial++;
						Traces.RemoveAtSwap(TraceIndex);
						TraceMap.Remove(TraceId);
					}
				}
			}

			// Check for added traces.
			{
				const int32 TraceCount = StoreClient->GetTraceCount();
				for (int32 TraceIndex = 0; TraceIndex < TraceCount; ++TraceIndex)
				{
					const Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfo(TraceIndex);
					if (TraceInfo != nullptr)
					{
						const uint32 TraceId = TraceInfo->GetId();
						TSharedPtr<FStoreBrowserTraceInfo>* TracePtrPtr = TraceMap.Find(TraceId);
						if (TracePtrPtr == nullptr)
						{
							TSharedPtr<FStoreBrowserTraceInfo> TracePtr = MakeShared<FStoreBrowserTraceInfo>();
							FStoreBrowserTraceInfo& Trace = *TracePtr;

							Trace.TraceId = TraceId;
							Trace.TraceIndex = TraceIndex;

							const FAnsiStringView AnsiTraceName = TraceInfo->GetName();
							Trace.Name = FString(AnsiTraceName.Len(), AnsiTraceName.GetData());

							Trace.Timestamp = FStoreBrowserTraceInfo::ConvertTimestamp(TraceInfo->GetTimestamp());
							Trace.Size = TraceInfo->GetSize();

							FScopeLock Lock(&TracesCriticalSection);
							TracesChangeSerial++;
							Traces.Add(TracePtr);
							TraceMap.Add(TraceId, TracePtr);
						}
					}
				}
			}
		}
	}

	StopwatchTotal.Update();
	const uint64 Step1 = StopwatchTotal.GetAccumulatedTimeMs();

	// Update the live trace sessions.
	{
		TArray<uint32> NotLiveTraces;

		for (const auto& KV : LiveTraceMap)
		{
			const uint32 TraceId = KV.Key;
			FStoreBrowserTraceInfo& Trace = *KV.Value;

			ensure(Trace.bIsLive);

			FDateTime Timestamp(0);
			uint64 Size = 0;
			const Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(TraceId);
			if (TraceInfo != nullptr)
			{
				Timestamp = FStoreBrowserTraceInfo::ConvertTimestamp(TraceInfo->GetTimestamp());
				Size = TraceInfo->GetSize();
			}

			const Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfoByTraceId(TraceId);
			if (SessionInfo != nullptr)
			{
				// The trace is still live.
				const uint32 IpAddress = SessionInfo->GetIpAddress();
				if (IpAddress != Trace.IpAddress || Timestamp != Trace.Timestamp || Size != Trace.Size)
				{
					FScopeLock Lock(&TracesCriticalSection);
					TracesChangeSerial++;
					Trace.ChangeSerial++;
					Trace.Timestamp = Timestamp;
					Trace.Size = Size;
					Trace.IpAddress = IpAddress;
				}
			}
			else
			{
				NotLiveTraces.Add(TraceId);

				// The trace is not live anymore.
				FScopeLock Lock(&TracesCriticalSection);
				TracesChangeSerial++;
				Trace.ChangeSerial++;
				Trace.Timestamp = Timestamp;
				Trace.Size = Size;
				Trace.bIsLive = false;
				Trace.IpAddress = 0;
			}
		}

		for (const uint32 TraceId : NotLiveTraces)
		{
			LiveTraceMap.Remove(TraceId);
		}
	}

	StopwatchTotal.Update();
	const uint64 Step2 = StopwatchTotal.GetAccumulatedTimeMs();

	// Check if we have new live sessions.
	{
		const uint32 SessionCount = StoreClient->GetSessionCount();
		for (uint32 SessionIndex = 0; SessionIndex < SessionCount; ++SessionIndex)
		{
			const Trace::FStoreClient::FSessionInfo* SessionInfo = StoreClient->GetSessionInfo(SessionIndex);
			if (SessionInfo != nullptr)
			{
				const uint32 TraceId = SessionInfo->GetTraceId();
				if (!LiveTraceMap.Find(TraceId))
				{
					TSharedPtr<FStoreBrowserTraceInfo>* TracePtrPtr = TraceMap.Find(TraceId);
					if (TracePtrPtr)
					{
						// This trace is a new live session.
						LiveTraceMap.Add(TraceId, *TracePtrPtr);

						const uint32 IpAddress = SessionInfo->GetIpAddress();

						FDateTime Timestamp(0);
						uint64 Size = 0;
						const Trace::FStoreClient::FTraceInfo* TraceInfo = StoreClient->GetTraceInfoById(TraceId);
						if (TraceInfo != nullptr)
						{
							Timestamp = FStoreBrowserTraceInfo::ConvertTimestamp(TraceInfo->GetTimestamp());
							Size = TraceInfo->GetSize();
						}

						FStoreBrowserTraceInfo& Trace = **TracePtrPtr;

						FScopeLock Lock(&TracesCriticalSection);
						TracesChangeSerial++;
						Trace.ChangeSerial++;
						Trace.Timestamp = Timestamp;
						Trace.Size = Size;
						Trace.bIsLive = true;
						Trace.IpAddress = IpAddress;
					}
				}
			}
		}
	}

	StopwatchTotal.Update();
	const uint64 Step3 = StopwatchTotal.GetAccumulatedTimeMs();

	// Check to see if we need to update metadata.
	{
		FStopwatch Stopwatch;
		Stopwatch.Start();

		int32 MetadataUpdateCount = 0; // for debugging

		//if (false)
		for (TSharedPtr<FStoreBrowserTraceInfo>& TracePtr : Traces)
		{
			if (!TracePtr->bIsMetadataUpdated)
			{
				MetadataUpdateCount++;
				UpdateMetadata(*TracePtr);
			}
		}

		Stopwatch.Stop();
		if (MetadataUpdateCount > 0)
		{
			UE_LOG(TraceInsights, Log, TEXT("[StoreBrowser] Metadata updated in %llu ms for %d trace(s)."), Stopwatch.GetAccumulatedTimeMs(), MetadataUpdateCount);
		}
	}

	StopwatchTotal.Stop();
	const uint64 TotalTime = StopwatchTotal.GetAccumulatedTimeMs();
	if (TotalTime > 5)
	{
		UE_LOG(TraceInsights, Log, TEXT("[StoreBrowser] Updated in %llu ms (%llu + %llu + %llu + %llu)."), TotalTime, Step1, Step2 - Step1, Step3 - Step2, TotalTime - Step3);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStoreBrowser::ResetTraces()
{
	if (Traces.Num() > 0)
	{
		FScopeLock Lock(&TracesCriticalSection);
		TracesChangeSerial++;
		Traces.Reset();
		TraceMap.Reset();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FStoreBrowser::UpdateMetadata(FStoreBrowserTraceInfo& Trace)
{
	//UE_LOG(TraceInsights, Log, TEXT("[StoreBrowser] Updating metadata for trace 0x%08X (%s)..."), TraceSession.TraceId, *TraceSession.Name.ToString());

	Trace::FStoreClient* StoreClient = GetStoreClient();
	if (StoreClient == nullptr)
	{
		return;
	}

	FPlatformProcess::SleepNoStats(0.0f);

	Trace::FStoreClient::FTraceData TraceData = StoreClient->ReadTrace(Trace.TraceId);
	if (!TraceData)
	{
		return;
	}

	struct FDataStream : public Trace::IInDataStream
	{
		virtual int32 Read(void* Data, uint32 Size) override
		{
			if (BytesRead >= 1024 * 1024)
			{
				return 0;
			}

			const int32 InnerBytesRead = Inner->Read(Data, Size);
			BytesRead += InnerBytesRead;

			return InnerBytesRead;
		}

		virtual void Close() override
		{
			Inner->Close();
		}

		int32 BytesRead = 0;
		Trace::IInDataStream* Inner;
	};

	FDataStream DataStream;
	DataStream.Inner = TraceData.Get();

	FDiagnosticsSessionAnalyzer Analyzer;
	Trace::FAnalysisContext Context;
	Context.AddAnalyzer(Analyzer);
	Context.Process(DataStream).Wait();

	// Update the FStoreBrowserTraceInfo object.
	{
		FScopeLock Lock(&TracesCriticalSection);
		TracesChangeSerial++;
		Trace.ChangeSerial++;
		Trace.bIsMetadataUpdated = true;
		if (Analyzer.Platform.Len() != 0)
		{
			Trace.Platform = Analyzer.Platform;
			Trace.AppName = Analyzer.AppName;
			Trace.CommandLine = Analyzer.CommandLine;
			Trace.ConfigurationType = static_cast<EBuildConfiguration>(Analyzer.ConfigurationType);
			Trace.TargetType = static_cast<EBuildTargetType>(Analyzer.TargetType);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights
