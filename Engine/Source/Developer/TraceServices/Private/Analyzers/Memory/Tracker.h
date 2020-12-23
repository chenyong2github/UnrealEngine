// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Config.h"
#include "MetadataDb.h"

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
		uint32				Tag;
		uint16				Alignment;
		bool				bIsRealloc;
		uint8				_Padding0;
	};

							FTracker(ISbifBuilder* InSbifBuilder);
							~FTracker();
	void					Begin();
	void					End();
	void					AddAlloc(uint64 Address, const FMetadata& TeamTada);
	void					AddFree(uint64 Address);
	uint32					GetCurrentSerial() const;

private:
	struct FSyncWait
	{
		FGraphEventRef		JobRef;
		void*				WaitParam;
	};

	FLaneInput*				GetLaneInput(uint64 Address);
	void					Update(bool bDispatch);
	void					ProcessRetirees(const FRetireeJobData* JobData);
	FLaneJobData*			Sync();
	void					Dispatch(bool bDoRehash=true);
	void					Provision();
	void					FinalizeWork(FLaneJobData* Data);
	void					Finalize();
	FMetadataDb				MetadataDb;
	FLane*					Lanes[FTrackerConfig::NumLanes];
	FLaneInput*				LaneInputs[FTrackerConfig::NumLanes];
	ISbifBuilder*			SbifBuilder;
	FSyncWait				SyncWait;
	uint32					Serial;
	uint32					SerialBias;
	uint32					ColumnShift;
	uint32					ColumnMask;
	uint32					NumColumns;
};

} // namespace TraceServices
