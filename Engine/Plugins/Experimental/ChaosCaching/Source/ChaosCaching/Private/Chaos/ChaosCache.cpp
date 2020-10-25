// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ChaosCache.h"
#include "Chaos/ChaosCachingPlugin.h"
#include "Components/PrimitiveComponent.h"

UChaosCache::UChaosCache()
	: CurrentRecordCount(0)
	, CurrentPlaybackCount(0)
{

}

void UChaosCache::FlushPendingFrames()
{
	QUICK_SCOPE_CYCLE_COUNTER(QSTAT_CacheFlushPendingFrames);
	bool bWroteParticleData = false;
	FPendingFrameWrite NewData;
	while(PendingWrites.Dequeue(NewData))
	{
		QUICK_SCOPE_CYCLE_COUNTER(QSTAT_CacheFlushSingleFrame);
		const int32 ParticleCount = NewData.PendingParticleData.Num();

		bWroteParticleData |= ParticleCount > 0;

		for(int32 Index = 0; Index < ParticleCount; ++Index)
		{
			const FPendingParticleWrite& ParticleData = NewData.PendingParticleData[Index];
			const int32 ParticleIndex = ParticleData.ParticleIndex;

			int32 TrackIndex = INDEX_NONE;
			if(!TrackToParticle.Find(ParticleIndex, TrackIndex))
			{
				TrackToParticle.Add(ParticleIndex);
				TrackIndex = ParticleTracks.AddDefaulted();
			}

			FPerParticleCacheData& TargetCacheData = ParticleTracks[TrackIndex];
			FParticleTransformTrack& PTrack = TargetCacheData.TransformData;

			if(PTrack.GetNumKeys() == 0)
			{
				// Initial write to this particle
				PTrack.BeginOffset = NewData.Time;
			}

			// Make sure we're actually appending to the track - shouldn't be adding data from the past
			if(ensure(PTrack.GetNumKeys() == 0 || NewData.Time > PTrack.KeyTimestamps.Last()))
			{
				PTrack.KeyTimestamps.Add(NewData.Time);

				// Append the transform (ignoring scale)
				FRawAnimSequenceTrack& RawTrack = PTrack.RawTransformTrack;
				RawTrack.ScaleKeys.Add(FVector(1.0f));
				RawTrack.PosKeys.Add(ParticleData.PendingTransform.GetTranslation());
				RawTrack.RotKeys.Add(ParticleData.PendingTransform.GetRotation());

				for(TPair<FName, float> CurveKeyPair : ParticleData.PendingCurveData)
				{
					FRichCurve& TargetCurve = TargetCacheData.CurveData.FindOrAdd(CurveKeyPair.Key);
					TargetCurve.AddKey(NewData.Time, CurveKeyPair.Value);
				}
			}
		}

		for(TTuple<FName, FCacheEventTrack>& PendingTrack : NewData.PendingEvents)
		{
			FCacheEventTrack* CacheTrack = EventTracks.Find(PendingTrack.Key);

			if(!CacheTrack)
			{
				CacheTrack = &EventTracks.Add(PendingTrack.Key, FCacheEventTrack(PendingTrack.Key, PendingTrack.Value.Struct));
			}

			CacheTrack->Merge(MoveTemp(PendingTrack.Value));
		}

		++NumRecordedFrames;
	}

	if(bWroteParticleData)
	{
		QUICK_SCOPE_CYCLE_COUNTER(QSTAT_CacheCalcDuration);
		float Min = TNumericLimits<float>::Max();
		float Max = -Min;
		for(const FPerParticleCacheData& ParticleData : ParticleTracks)
		{
			Min = FMath::Min(Min, ParticleData.TransformData.GetBeginTime());
			Max = FMath::Max(Max, ParticleData.TransformData.GetEndTime());
		}

		RecordedDuration = Max - Min;
	}
}

FCacheUserToken UChaosCache::BeginRecord(UPrimitiveComponent* InComponent, FGuid InAdapterId)
{
	// First make sure we're valid to record
	int32 OtherRecordersCount = CurrentRecordCount.fetch_add(1);
	if(OtherRecordersCount == 0)
	{
		// We're the only recorder
		if(CurrentPlaybackCount.load() == 0)
		{
			// And there's no playbacks, we can proceed
			// Setup the cache to begin recording
			RecordedDuration = 0.0f;
			NumRecordedFrames = 0;
			ParticleTracks.Reset();
			TrackToParticle.Reset();
			CurveData.Reset();
			EventTracks.Reset();

			PendingWrites.Empty();

			// Initialise the spawnable template to handle the provided component
			BuildSpawnableFromComponent(InComponent);

			// Build a compatibility hash for the component state.
			AdapterGuid = InAdapterId;

			return FCacheUserToken(true, true, this);
		}
		else
		{
			UE_LOG(LogChaosCache, Warning, TEXT("Failed to open cache %s for record, it was the only recorder but the cache was open for playback."), *GetPathName());
			CurrentRecordCount--;
		}
	}
	else
	{
		UE_LOG(LogChaosCache, Warning, TEXT("Failed to open cache %s for record, the cache was already open for record."), *GetPathName());
		CurrentRecordCount--;
	}

	return FCacheUserToken(false, true, this);
}

void UChaosCache::EndRecord(FCacheUserToken& InOutToken)
{
	if(InOutToken.IsOpen() && InOutToken.Owner == this)
	{
		FlushPendingFrames();
		// Cache now complete, process data

		// Invalidate the token
		InOutToken.bIsOpen = false;
		InOutToken.Owner = nullptr;
		CurrentRecordCount--;
	}
	else
	{
		if(InOutToken.Owner)
		{
			UE_LOG(LogChaosCache, Warning, TEXT("Attempted to close a recording session with a token from an external cache."));
		}
		else
		{
			UE_LOG(LogChaosCache, Warning, TEXT("Attempted to close a recording session with an invalid token"));
		}
	}
}

FCacheUserToken UChaosCache::BeginPlayback()
{
	CurrentPlaybackCount++;
	if(CurrentRecordCount.load() == 0)
	{
		// We can playback from this cache as it isn't open for record
		return FCacheUserToken(true, false, this);
	}
	else
	{
		CurrentPlaybackCount--;
	}

	return FCacheUserToken(false, false, this);
}

void UChaosCache::EndPlayback(FCacheUserToken& InOutToken)
{
	if(InOutToken.IsOpen() && InOutToken.Owner == this)
	{
		// Invalidate the token
		InOutToken.bIsOpen = false;
		InOutToken.Owner = nullptr;
		CurrentPlaybackCount--;
	}
	else
	{
		if(InOutToken.Owner)
		{
			UE_LOG(LogChaosCache, Warning, TEXT("Attempted to close a playback session with a token from an external cache."));
		}
		else
		{
			UE_LOG(LogChaosCache, Warning, TEXT("Attempted to close a playback session with an invalid token"));
		}
	}
}

void UChaosCache::AddFrame_Concurrent(FPendingFrameWrite&& InFrame)
{
	PendingWrites.Enqueue(MoveTemp(InFrame));
}

float UChaosCache::GetDuration() const
{
	return RecordedDuration;
}

FCacheEvaluationResult UChaosCache::Evaluate(const FCacheEvaluationContext& InContext)
{
	QUICK_SCOPE_CYCLE_COUNTER(QSTAT_CacheEval);

	FCacheEvaluationResult Result;

	if(CurrentPlaybackCount.load() == 0)
	{
		// No valid playback session
		UE_LOG(LogChaosCache, Warning, TEXT("Attempted to evaluate a cache that wasn't opened for playback"));
		return Result;
	}

	Result.EvaluatedTime = InContext.TickRecord.GetTime();

	if(!InContext.bEvaluateTransform && !InContext.bEvaluateCurves && !InContext.bEvaluateEvents)
	{
		// no evaluation requested
		return Result;
	}

	const int32 NumProvidedIndices = InContext.EvaluationIndices.Num();

	if(NumProvidedIndices > 0 && NumProvidedIndices < ParticleTracks.Num())
	{
		if(InContext.bEvaluateTransform)
		{
			Result.Transform.SetNum(NumProvidedIndices);
		}

		if(InContext.bEvaluateCurves)
		{
			Result.Curves.SetNum(NumProvidedIndices);
		}

		for(int32 EvalIndex = 0; EvalIndex < NumProvidedIndices; ++EvalIndex)
		{
			int32 CacheIndex = InContext.EvaluationIndices[EvalIndex];
			if(ensure(ParticleTracks.IsValidIndex(CacheIndex)))
			{
				FTransform* EvalTransform = nullptr;
				TMap<FName, float>* EvalCurves = nullptr;

				if(InContext.bEvaluateTransform)
				{
					EvalTransform = &Result.Transform[EvalIndex];
				}

				if(InContext.bEvaluateCurves)
				{
					EvalCurves = &Result.Curves[EvalIndex];
				}

				if(TrackToParticle.IsValidIndex(CacheIndex))
				{
					Result.ParticleIndices.Add(TrackToParticle[CacheIndex]);
				}
				else
				{
					Result.ParticleIndices.Add(INDEX_NONE);
				}

				EvaluateSingle(CacheIndex, InContext.TickRecord, EvalTransform, EvalCurves);
			}
		}
	}
	else
	{
		const int32 NumParticles = ParticleTracks.Num();
		
		if(InContext.bEvaluateTransform)
		{
			Result.Transform.Reserve(NumParticles);
		}

		if(InContext.bEvaluateCurves)
		{
			Result.Curves.Reserve(NumParticles);
		}

		for(int32 Index = 0; Index < NumParticles; ++Index)
		{
			if(ParticleTracks[Index].TransformData.BeginOffset > InContext.TickRecord.GetTime())
			{
				// Track hasn't begun yet so skip evaluation
				continue;
			}

			FTransform* EvalTransform = nullptr;
			TMap<FName, float>* EvalCurves = nullptr;

			if(InContext.bEvaluateTransform)
			{
				Result.Transform.AddUninitialized();
				EvalTransform = &Result.Transform.Last();
			}

			if(InContext.bEvaluateCurves)
			{
				Result.Curves.AddUninitialized();
				EvalCurves = &Result.Curves.Last();
			}

			if(TrackToParticle.IsValidIndex(Index))
			{
				Result.ParticleIndices.Add(TrackToParticle[Index]);
			}
			else
			{
				Result.ParticleIndices.Add(INDEX_NONE);
			}

			EvaluateSingle(Index, InContext.TickRecord, EvalTransform, EvalCurves);
		}
	}

	if(InContext.bEvaluateEvents)
	{
		Result.Events.Reserve(EventTracks.Num());
		EvaluateEvents(InContext.TickRecord, Result.Events);
	}

	// Update the tick record on completion for the next run
	InContext.TickRecord.LastTime = InContext.TickRecord.GetTime();
	InContext.TickRecord.CurrentDt = 0.0f;

	return Result;
}

void UChaosCache::BuildSpawnableFromComponent(UPrimitiveComponent* InComponent)
{
	Spawnable.DuplicatedTemplate = StaticDuplicateObject(InComponent, this);
	Spawnable.InitialTransform = InComponent->GetComponentToWorld();
}

const FCacheSpawnableTemplate& UChaosCache::GetSpawnableTemplate() const
{
	return Spawnable;
}

void UChaosCache::EvaluateSingle(int32 InIndex, FPlaybackTickRecord& InTickRecord, FTransform* OutOptTransform, TMap<FName, float>* OutOptCurves)
{
	// check to satisfy SA, external callers check validity in Evaluate
	checkSlow(ParticleTracks.IsValidIndex(InIndex));
	FPerParticleCacheData& Data = ParticleTracks[InIndex];

	if(OutOptTransform)
	{
		EvaluateTransform(Data, InTickRecord.GetTime(), *OutOptTransform);
		(*OutOptTransform) = (*OutOptTransform) * InTickRecord.SpaceTransform;
	}

	if(OutOptCurves)
	{
		EvaluateCurves(Data, InTickRecord.GetTime(), *OutOptCurves);
	}
}

void UChaosCache::EvaluateTransform(const FPerParticleCacheData& InData, float InTime, FTransform& OutTransform)
{
	OutTransform = InData.TransformData.Evaluate(InTime);
}

void UChaosCache::EvaluateCurves(const FPerParticleCacheData& InData, float InTime, TMap<FName, float>& OutCurves)
{
	for(const TPair<FName, FRichCurve>& Curve : InData.CurveData)
	{
		OutCurves.FindOrAdd(Curve.Key) = Curve.Value.Eval(InTime, 0.0f);
	}
}

void UChaosCache::EvaluateEvents(FPlaybackTickRecord& InTickRecord, TMap<FName, TArray<FCacheEventHandle>>& OutEvents)
{
	OutEvents.Reset();

	for(TTuple<FName, FCacheEventTrack>& Track : EventTracks)
	{
		FCacheEventTrack& TrackRef = Track.Value;
		if(TrackRef.Num() == 0)
		{
			continue;
		}

		int32* BeginIndexPtr = InTickRecord.LastEventPerTrack.Find(Track.Key);
		const int32 BeginIndex = BeginIndexPtr ? *BeginIndexPtr : 0;

		TArrayView<float> TimeStampView(&TrackRef.TimeStamps[BeginIndex], TrackRef.TimeStamps.Num() - BeginIndex);

		int32 BeginEventIndex = Algo::UpperBound(TimeStampView, InTickRecord.LastTime) + BeginIndex;
		const int32 EndEventIndex = Algo::UpperBound(TimeStampView, InTickRecord.GetTime()) + BeginIndex;

		TArray<FCacheEventHandle> NewHandles;
		NewHandles.Reserve(EndEventIndex - BeginEventIndex);

		// Add anything we found
		while(BeginEventIndex != EndEventIndex)
		{
			NewHandles.Add(TrackRef.GetEventHandle(BeginEventIndex));
			++BeginEventIndex;
		}

		// If we added any handles then we must have a new index for the lastevent tracker in the tick record
		if(NewHandles.Num() > 0)
		{
			int32& OutBeginIndex = BeginIndexPtr ? *BeginIndexPtr : InTickRecord.LastEventPerTrack.Add(Track.Key);
			OutBeginIndex = NewHandles.Last().Index;

			// Push to the result container
			OutEvents.Add(TTuple<FName, TArray<FCacheEventHandle>>(Track.Key, MoveTemp(NewHandles)));
		}
	}
}

FTransform FParticleTransformTrack::Evaluate(float InCacheTime) const
{
	QUICK_SCOPE_CYCLE_COUNTER(QSTAT_EvalParticleTransformTrack);
	const int32 NumKeys = GetNumKeys();

	if(NumKeys > 0)
	{
		if(InCacheTime < BeginOffset)
		{
			// Take first key
			return FTransform(RawTransformTrack.RotKeys[0], RawTransformTrack.PosKeys[0]);
		}
		else if(InCacheTime > KeyTimestamps.Last())
		{
			// Take last key
			return FTransform(RawTransformTrack.RotKeys.Last(), RawTransformTrack.PosKeys.Last());
		}
		else
		{
			// Valid in-range, evaluate
			if(NumKeys == 1)
			{
				return FTransform(RawTransformTrack.RotKeys[0], RawTransformTrack.PosKeys[0]);
			}

			// Find the first key with a timestamp greater than InCacheTIme
			int32 IndexBeyond = 0;
			{
				QUICK_SCOPE_CYCLE_COUNTER(QSTAT_UpperBound);
				IndexBeyond = Algo::UpperBound(KeyTimestamps, InCacheTime);
			}

			if(IndexBeyond == INDEX_NONE || IndexBeyond >= KeyTimestamps.Num())
			{
				// Must be equal to the last key
				return FTransform(RawTransformTrack.RotKeys.Last(), RawTransformTrack.PosKeys.Last());
			}

			const int32 IndexBefore = IndexBeyond - 1;

			if(IndexBefore == INDEX_NONE)
			{
				// Must have been equal to first key
				return FTransform(RawTransformTrack.RotKeys[0], RawTransformTrack.PosKeys[0]);
			}

			// Need to interpolate
			const float Interval = KeyTimestamps[IndexBeyond] - KeyTimestamps[IndexBefore];
			const float Fraction = (InCacheTime - KeyTimestamps[IndexBefore]) / Interval;

			// Slerp rotation - lerp translation
			return FTransform(FQuat::Slerp(RawTransformTrack.RotKeys[IndexBefore], RawTransformTrack.RotKeys[IndexBeyond], Fraction),
							  FMath::Lerp(RawTransformTrack.PosKeys[IndexBefore], RawTransformTrack.PosKeys[IndexBeyond], Fraction));
		}
	}

	return FTransform::Identity;
}

const int32 FParticleTransformTrack::GetNumKeys() const
{
	return KeyTimestamps.Num();
}

const float FParticleTransformTrack::GetDuration() const
{
	if(GetNumKeys() > 1)
	{
		return KeyTimestamps.Last() - KeyTimestamps[0];
	}
	return 0.0f;
}

const float FParticleTransformTrack::GetBeginTime() const
{
	const int32 NumKeys = GetNumKeys();
	if(NumKeys > 0)
	{
		return KeyTimestamps[0];
	}

	return 0.0f;
}

const float FParticleTransformTrack::GetEndTime() const
{
	const int32 NumKeys = GetNumKeys();
	if(NumKeys > 0)
	{
		return KeyTimestamps.Last();
	}

	return 0.0f;
}
