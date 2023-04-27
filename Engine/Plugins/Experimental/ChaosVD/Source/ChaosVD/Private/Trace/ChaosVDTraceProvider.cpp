// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/ChaosVDTraceProvider.h"

#include "Chaos/ImplicitObject.h"
#include "ChaosVDRecording.h"
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

void FChaosVDTraceProvider::AddFrame(const int32 InSolverID, FChaosVDSolverFrameData&& FrameData)
{
	if (InternalRecording.IsValid())
	{
		InternalRecording->AddFrameForSolver(InSolverID, MoveTemp(FrameData));
	}
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetFrame(const int32 InSolverID, const int32 FrameNumber) const
{
	return InternalRecording.IsValid() ? InternalRecording->GetFrameForSolver(InSolverID, FrameNumber) : nullptr;
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetLastFrame(const int32 InSolverID) const
{
	if (InternalRecording.IsValid() && InternalRecording->GetAvailableFramesNumber(InSolverID) > 0)
	{
		const int32 AvailableFramesNumber = InternalRecording->GetAvailableFramesNumber(InSolverID);

		if (AvailableFramesNumber != INDEX_NONE)
		{
			return GetFrame(InSolverID, InternalRecording->GetAvailableFramesNumber(InSolverID) - 1);
		}
	}

	return nullptr;
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

			// TODO: Create a system to register external "Data handlers" with the logic on how serialize each type
			// This should not be done here but as this is the only type we have is ok for now
			if (UnprocessedData->TypeName == TEXT("FImplicitObject"))
			{
				Chaos::TSerializablePtr<Chaos::FImplicitObject> Geometry;

				FMemoryReader MemeReader(*RawData);
				Chaos::FChaosArchive Ar(MemeReader);

				if (ChaosContext.IsValid())
				{
					Ar.SetContext(MoveTemp(ChaosContext));
					Ar << Geometry;
					ChaosContext = Ar.StealContext();
				}
				else
				{
					Ar << Geometry;
					ChaosContext = Ar.StealContext();
				}

				InternalRecording->AddImplicitObject(ChaosContext->ObjToTag[Geometry.Get()], Geometry.Get());
			}
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
