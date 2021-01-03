// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/TaskGraphInterfaces.h"
#include "Config.h"

namespace TraceServices {

class FLane;
class FLaneInput;
struct FLaneJobData;
class FRetiree;
struct FRetireeJobData;
class IRetireeSink;

////////////////////////////////////////////////////////////////////////////////
class FTracker
{
public:
							FTracker(IRetireeSink* InRetireeSink);
							~FTracker();
	void					Begin();
	void					End();
	void					AddAlloc(uint64 Address, uint32 MeatdataId);
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
	FLane*					Lanes[FTrackerConfig::NumLanes];
	FLaneInput*				LaneInputs[FTrackerConfig::NumLanes];
	IRetireeSink*			RetireeSink;
	FSyncWait				SyncWait;
	uint32					Serial;
	uint32					SerialBias;
};

} // namespace TraceServices
