// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/ChaosVDTraceProvider.h"

#include "Chaos/ImplicitObject.h"
#include "ChaosVDRecording.h"
#include "ChaosVisualDebugger/ChaosVisualDebuggerTrace.h"
#include "Compression/OodleDataCompressionUtil.h"
#include "Serialization/MemoryReader.h"

FName FChaosVDTraceProvider::ProviderName("ChaosVDProvider");

FChaosVDTraceProvider::FChaosVDTraceProvider(TraceServices::IAnalysisSession& InSession): Session(InSession)
{
}

void FChaosVDTraceProvider::CreateRecordingInstanceForSession(const FString& InSessionName)
{
	DeleteRecordingInstanceForSession();

	InternalRecording = MakeShared<FChaosVDRecording>();
	InternalRecording->SessionName = InSessionName;
}

void FChaosVDTraceProvider::DeleteRecordingInstanceForSession()
{
	InternalRecording.Reset();
}

void FChaosVDTraceProvider::AddSolverFrame(const int32 InSolverID, FChaosVDSolverFrameData&& FrameData)
{
	if (InternalRecording.IsValid())
	{
		InternalRecording->AddFrameForSolver(InSolverID, MoveTemp(FrameData));
	}
}

void FChaosVDTraceProvider::AddGameFrame(FChaosVDGameFrameData&& FrameData)
{
	if (InternalRecording.IsValid())
	{
		// In PIE, we can have a lot of empty frames at the beginning, so we discard them here
		if (InternalRecording->GetAvailableSolvers().IsEmpty())
		{
			if (FChaosVDGameFrameData* GameFrame = InternalRecording->GetLastGameFrameData())
			{
				GameFrame = &FrameData;
				return;
			}
		}

		InternalRecording->AddGameFrameData(MoveTemp(FrameData));
	}
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetSolverFrame(const int32 InSolverID, const int32 FrameNumber) const
{
	return InternalRecording.IsValid() ? InternalRecording->GetSolverFrameData(InSolverID, FrameNumber) : nullptr;
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetLastSolverFrame(const int32 InSolverID) const
{
	if (InternalRecording.IsValid() && InternalRecording->GetAvailableSolverFramesNumber(InSolverID) > 0)
	{
		const int32 AvailableFramesNumber = InternalRecording->GetAvailableSolverFramesNumber(InSolverID);

		if (AvailableFramesNumber != INDEX_NONE)
		{
			return GetSolverFrame(InSolverID, InternalRecording->GetAvailableSolverFramesNumber(InSolverID) - 1);
		}
	}

	return nullptr;
}

FChaosVDGameFrameData* FChaosVDTraceProvider::GetSolverFrame(uint64 FrameStartCycle) const
{
	FChaosVDGameFrameData* FoundFrameData = nullptr;

	if (InternalRecording.IsValid() && InternalRecording->GetAvailableGameFrames().Num() > 0)
	{
		return InternalRecording->GetGameFrameDataAtCycle(FrameStartCycle);
	}
	
	return FoundFrameData;
}

FChaosVDGameFrameData* FChaosVDTraceProvider::GetLastGameFrame() const
{
	return InternalRecording.IsValid() ? InternalRecording->GetLastGameFrameData() : nullptr;
}

FChaosVDBinaryDataContainer& FChaosVDTraceProvider::FindOrAddUnprocessedData(const int32 DataID)
{
	if (const TSharedPtr<FChaosVDBinaryDataContainer>* UnprocessedData = UnprocessedDataByID.Find(DataID))
	{
		check(UnprocessedData->IsValid());
		return *UnprocessedData->Get();
	}
	else
	{
		const TSharedPtr<FChaosVDBinaryDataContainer> DataContainer = MakeShared<FChaosVDBinaryDataContainer>(DataID);
		UnprocessedDataByID.Add(DataID, DataContainer);
		return *DataContainer.Get();
	}
}

void FChaosVDTraceProvider::SetBinaryDataReadyToUse(const int32 DataID)
{
	if (const TSharedPtr<FChaosVDBinaryDataContainer>* UnprocessedDataPtr = UnprocessedDataByID.Find(DataID))
	{
		const TSharedPtr<FChaosVDBinaryDataContainer> UnprocessedData = *UnprocessedDataPtr;
		if (UnprocessedData.IsValid())
		{
			UnprocessedData->bIsReady = true;

			BinaryDataReadyDelegate.Broadcast(*UnprocessedDataPtr);

			const TArray<uint8>* RawData = nullptr;
			TArray<uint8> UncompressedData;
			if (UnprocessedData->bIsCompressed)
			{
				UncompressedData.Reserve(UnprocessedData->UncompressedSize);
				FOodleCompressedArray::DecompressToTArray(UncompressedData, UnprocessedData->RawData);
				RawData = &UncompressedData;
			}
			else
			{
				RawData = &UnprocessedData->RawData;
			}

#if WITH_CHAOS_VISUAL_DEBUGGER
			// TODO: Create a system to register external "Data handlers" with the logic on how serialize each type
			// This should not be done here but as this is the only type we have is ok for now
			if (UnprocessedData->TypeName == TEXT("FChaosVDImplicitObjectDataWrapper"))
			{	
				Chaos::TSerializablePtr<Chaos::FImplicitObject> Geometry;

				FMemoryReader MemeReader(*RawData);
				Chaos::FChaosArchive Ar(MemeReader);

				FChaosVDImplicitObjectDataWrapper WrappedGeometryData;
				WrappedGeometryData.Serialize(Ar);

				InternalRecording->AddImplicitObject(WrappedGeometryData.Hash, WrappedGeometryData.ImplicitObject.Get());
			}
#endif
		}
	}
	else
	{
		ensure(false);	
	}
}

TSharedPtr<FChaosVDRecording> FChaosVDTraceProvider::GetRecordingForSession() const
{
	return InternalRecording;
}
