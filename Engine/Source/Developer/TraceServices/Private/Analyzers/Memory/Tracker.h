// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Config.h"
#include "MetadataDb.h"

namespace Trace {
namespace TraceServices {

class FLane;
class FLaneInput;
struct FLaneJobData;
class FRetiree;
struct FRetireeJobData;
class ISbifBuilder;

////////////////////////////////////////////////////////////////////////////////
class FTracker
{
public:
	struct FMetadata
	{
		uint64				Owner;
		uint64				Size; 
		uint32				Alignment;
		bool				bIsRealloc;
	};

							FTracker(ISbifBuilder* InSbifBuilder);
							~FTracker();
	void					Begin();
	void					End();
	void					AddAlloc(uint64 Address, const FMetadata& TeamTada);
	void					AddFree(uint64 Address);
	uint32					GetCurrentSerial() const;

private:
	FLaneInput*				GetLaneInput(uint64 Address);
	void					Update(bool bDispatch);
	void					ProcessRetirees(const FRetireeJobData* JobData);
	void					RetireAllocs(const FRetireeJobData* Data, uint32 Column, uint32 Depth, const FRetiree* __restrict Left, const FRetiree* __restrict Right);
	FLaneJobData*			Sync();
	void					Dispatch(bool bDoRehash=true);
	void					Provision();
	void					FinalizeWork(FLaneJobData* Data);
	void					Finalize();
	FMetadataDb				MetadataDb;
	FLane*					Lanes[FTrackerConfig::NumLanes];
	FLaneInput*				LaneInputs[FTrackerConfig::NumLanes];
	ISbifBuilder*			SbifBuilder;
	UPTRINT					SyncWait;
	uint32					Serial;
	uint32					SerialBias;
	uint32					ColumnShift;
	uint32					ColumnMask;
	uint32					NumColumns;

#if defined(PROF_ON)
	int32					AllocDelta = 0;
	int32					FreeDelta = 0;
	int32					Running = 0;
#endif
};

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
