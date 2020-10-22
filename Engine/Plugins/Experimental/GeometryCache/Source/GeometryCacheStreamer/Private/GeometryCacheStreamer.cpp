// Copyright Epic Games, Inc. All Rights Reserved.

#include "IGeometryCacheStreamer.h"
#include "Async/TaskGraphInterfaces.h"
#include "Containers/Ticker.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GeometryCacheMeshData.h"
#include "IGeometryCacheStream.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "GeometryCacheStreamer"

class FGeometryCacheStreamer : public IGeometryCacheStreamer
{
public:
	FGeometryCacheStreamer();
	virtual ~FGeometryCacheStreamer();

	void Tick(float Time);

	//~ Begin IGeometryCacheStreamer Interface
	static IGeometryCacheStreamer& Get();

	virtual void RegisterTrack(UGeometryCacheTrack* AbcTrack, IGeometryCacheStream* Stream) override;
	virtual void UnregisterTrack(UGeometryCacheTrack* AbcTrack) override;
	virtual bool IsTrackRegistered(UGeometryCacheTrack* AbcTrack) const override;
	virtual bool TryGetFrameData(UGeometryCacheTrack* AbcTrack, int32 FrameIndex, FGeometryCacheMeshData& OutMeshData) override;
	//~ End IGeometryCacheStreamer Interface

private:
	FDelegateHandle TickHandle;

	typedef TMap<UGeometryCacheTrack*, IGeometryCacheStream*> FTracksToStreams;
	FTracksToStreams TracksToStreams;

	const int32 MaxReads;
	int32 NumReads;
	int32 CurrentIndex;

	TSharedPtr<SNotificationItem> StreamingNotification;
};

FGeometryCacheStreamer::FGeometryCacheStreamer()
: MaxReads(FTaskGraphInterface::Get().GetNumWorkerThreads())
, NumReads(0)
, CurrentIndex(0)

{
	TickHandle = FTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float Time)
		{
			this->Tick(Time);
			return true;
		})
	);
}

FGeometryCacheStreamer::~FGeometryCacheStreamer()
{
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}
}

void FGeometryCacheStreamer::Tick(float Time)
{
	// The sole purpose of the Streamer is to schedule the streams for read at every engine loop

	// First step is to process the results from the previously scheduled read requests
	// to figure out the number of reads still in progress
	int32 NumFramesToStream = 0;
	for (FTracksToStreams::TIterator Iterator = TracksToStreams.CreateIterator(); Iterator; ++Iterator)
	{
		TArray<int32> FramesCompleted;
		IGeometryCacheStream* Stream = Iterator->Value;
		Stream->UpdateRequestStatus(FramesCompleted);
		NumReads -= FramesCompleted.Num();
		NumFramesToStream += Stream->GetFramesNeeded().Num();
	}

	// Now, schedule new read requests according to the number of concurrent reads available
	const int32 NumStreams = TracksToStreams.Num();
	int32 AvailableReads = MaxReads - NumReads;
	if (NumStreams > 0 && AvailableReads > 0)
	{
		TArray<IGeometryCacheStream*> Streams;
		TracksToStreams.GenerateValueArray(Streams);

		// Streams are checked round robin until there are no more reads available
		// or no stream can handle more read requests
		// Note that the round robin starts from where it left off in the previous Tick
		TBitArray<> StreamsToCheck(true, NumStreams);
		for (; AvailableReads > 0 && StreamsToCheck.Contains(true); ++CurrentIndex)
		{
			// Handle looping but also the case where the number of streams has decreased
			if (CurrentIndex >= NumStreams)
			{
				CurrentIndex = 0;
			}

			if (!StreamsToCheck[CurrentIndex])
			{
				continue;
			}

			IGeometryCacheStream* Stream = Streams[CurrentIndex];
			const TArray<int32>& FramesNeeded = Stream->GetFramesNeeded();
			if (FramesNeeded.Num() > 0)
			{
				if (Stream->RequestFrameData(FramesNeeded[0]))
				{
					// Stream was able to handle the read request so there's one less available
					++NumReads;
					--AvailableReads;
				}
				else
				{
					// Stream cannot handle more read request, don't need to check it again
					StreamsToCheck[CurrentIndex] = false;
				}
			}
			else
			{
				// Stream doesn't need any frame to be read, don't need to check it again
				StreamsToCheck[CurrentIndex] = false;
			}
		}
	}

	// Display a streaming progress notification if there are any frames to stream
	if (!StreamingNotification.IsValid() && NumFramesToStream > 0)
	{
		FText UpdateText = FText::Format(LOCTEXT("GeoCacheStreamingUpdate", "Streaming GeometryCache: {0} frames remaining"), FText::AsNumber(NumFramesToStream));
		FNotificationInfo Info(UpdateText);
		Info.bFireAndForget = false;
		Info.bUseSuccessFailIcons = false;
		Info.bUseLargeFont = false;

		StreamingNotification = FSlateNotificationManager::Get().AddNotification(Info);
		if (StreamingNotification.IsValid())
		{
			StreamingNotification->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}

	if (StreamingNotification.IsValid())
	{
		// Update or remove the progress notification
		if (NumFramesToStream > 0)
		{
			FText UpdateText = FText::Format(LOCTEXT("GeoCacheStreamingUpdate", "Streaming GeometryCache: {0} frames remaining"), FText::AsNumber(NumFramesToStream));
			StreamingNotification->SetText(UpdateText);
		}
		else
		{
			FText CompletedText = LOCTEXT("GeoCacheStreamingFinished", "Finished streaming GeometryCache");
			StreamingNotification->SetText(CompletedText);
			StreamingNotification->SetCompletionState(SNotificationItem::CS_Success);
			StreamingNotification->ExpireAndFadeout();
			StreamingNotification = nullptr;
		}
	}
}

void FGeometryCacheStreamer::RegisterTrack(UGeometryCacheTrack* AbcTrack, IGeometryCacheStream* Stream)
{
	check(AbcTrack && Stream && !TracksToStreams.Contains(AbcTrack));

	TracksToStreams.Add(AbcTrack, Stream);
}

void FGeometryCacheStreamer::UnregisterTrack(UGeometryCacheTrack* AbcTrack)
{
	if (IGeometryCacheStream** Stream = TracksToStreams.Find(AbcTrack))
	{
		int32 NumRequests = (*Stream)->CancelRequests();
		NumReads -= NumRequests;

		delete *Stream;
		TracksToStreams.Remove(AbcTrack);
	}
}

bool FGeometryCacheStreamer::IsTrackRegistered(UGeometryCacheTrack* AbcTrack) const
{
	return TracksToStreams.Contains(AbcTrack);
}

bool FGeometryCacheStreamer::TryGetFrameData(UGeometryCacheTrack* AbcTrack, int32 FrameIndex, FGeometryCacheMeshData& OutMeshData)
{
	if (IGeometryCacheStream** Stream = TracksToStreams.Find(AbcTrack))
	{
		if ((*Stream)->GetFrameData(FrameIndex, OutMeshData))
		{
			return true;
		}
	}
	return false;
}

IGeometryCacheStreamer& IGeometryCacheStreamer::Get()
{
	static FGeometryCacheStreamer Streamer;
	return Streamer;
}

#undef LOCTEXT_NAMESPACE