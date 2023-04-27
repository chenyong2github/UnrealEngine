// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/SharedPointer.h"
#include "Chaos/ParticleHandleFwd.h"
#include "Delegates/Delegate.h"
#include "Containers/UnrealString.h"
#include "Containers/Array.h"

class FChaosVDEngine;
struct FChaosVDSolverFrameData;
struct FChaosVDRecording;

namespace Chaos
{
	class FChaosArchiveContext;
}

struct FChaosVDBinaryDataContainer
{
	FChaosVDBinaryDataContainer(const int32 InDataID)
		: DataID(InDataID)
	{
	}

	int32 DataID;
	bool bIsReady = false;
	bool bIsCompressed = false;
	uint32 UncompressedSize = 0;
	FString TypeName;
	TArray<uint8> RawData;
};

struct FChaosVDTraceSessionData
{
	TSharedPtr<FChaosVDRecording> InternalRecordingsMap;

	TMap<int32, TSharedPtr<FChaosVDBinaryDataContainer>> UnprocessedDataByID;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnBinaryDataReady, TSharedPtr<FChaosVDBinaryDataContainer>)

/** Provider class for Chaos VD trace recordings.
 * It stores and handles rebuilt recorded frame data from Trace events
 * dispatched by the Chaos VD Trace analyzer
 */
class FChaosVDTraceProvider : public TraceServices::IProvider
{
public:
	
	static FName ProviderName;

	FChaosVDTraceProvider(TraceServices::IAnalysisSession& InSession);

	void CreateRecordingInstanceForSession(const FString& InSessionName);
	void DeleteRecordingInstanceForSession();
	void AddFrame(const int32 InSolverGUID, FChaosVDSolverFrameData&& FrameData);
	FChaosVDSolverFrameData* GetFrame(const int32 InSolverGUID, const int32 FrameNumber) const;
	FChaosVDSolverFrameData* GetLastFrame(const int32 InSolverGUID) const;

	FChaosVDBinaryDataContainer& FindOrAddUnprocessedData(const int32 DataID);

	void SetBinaryDataReadyToUse(const int32 DataID);

	TSharedPtr<FChaosVDRecording> GetRecordingForSession() const;
	
	FOnBinaryDataReady& OnBinaryDataReady() { return BinaryDataReadyDelegate; }

private:
	TraceServices::IAnalysisSession& Session;

	TSharedPtr<FChaosVDRecording> InternalRecording;

	TMap<int32, TSharedPtr<FChaosVDBinaryDataContainer>> UnprocessedDataByID;
	
	FOnBinaryDataReady BinaryDataReadyDelegate;

	TUniquePtr<Chaos::FChaosArchiveContext> ChaosContext;
};
