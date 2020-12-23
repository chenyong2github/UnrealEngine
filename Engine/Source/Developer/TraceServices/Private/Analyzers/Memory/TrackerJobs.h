// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lane.h"

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class alignas(16) FRetiree
{
public:
	void			Set(uint32 Start, uint32 EndBiased, uint64 InAddress, uint32 InMetadataId);
	uint64			GetSortKey() const				{ return SortKey; }
	uint64			GetAddress() const				{ return Address; }
	uint64			GetStartSerial() const			{ return StartSerial; }
	uint32			GetEndSerial(uint32 Bias) const	{ return EndSerialBiased + Bias; }
	uint32			GetEndSerialBiased() const		{ return EndSerialBiased; }
	uint32			GetMetadataId() const			{ return MetadataId; }

private:
	union
	{
		struct
		{
			uint64	Address			: 44;
			uint64	EndSerialBiased	: 20;
		};
		uint64		SortKey; // end-serial is most significant.
	};
	uint64			StartSerial		: 36;
	uint64			MetadataId		: 28;
};

////////////////////////////////////////////////////////////////////////////////
inline void FRetiree::Set(uint32 Start, uint32 EndBiased, uint64 InAddress, uint32 InMetadataId)
{
	Address = InAddress;
	EndSerialBiased = EndBiased;
	StartSerial = Start;
	MetadataId = InMetadataId;
}



////////////////////////////////////////////////////////////////////////////////
struct FRetirees
{
	static_assert(sizeof(FRetiree) == 16, "");
	
	uint32			Max;
	uint32			Num = 0;
	FRetiree		Items[];
};

////////////////////////////////////////////////////////////////////////////////
struct FBaseJobData
{
	FBaseJobData*		Next;
};

////////////////////////////////////////////////////////////////////////////////
struct FRetireeJobData
	: public FBaseJobData
{
	FRetirees*			Retirees;
	uint32				SerialBias;
};

////////////////////////////////////////////////////////////////////////////////
struct FLaneJobData
	: public FRetireeJobData
{
	FLane*				Lane;
	FLaneInput*			Input;
	FLaneItemView		SetUpdates;
};

////////////////////////////////////////////////////////////////////////////////
struct FLeakJobData
	: public FRetireeJobData
{
	const FLaneItemSet* ActiveSet;
};

////////////////////////////////////////////////////////////////////////////////
void LaneRehashJob(void* Data);
void LaneInputJob(FLaneJobData* Data);
void LaneUpdateJob(FLaneJobData* Data);
void LaneRetireeJob(FRetireeJobData* Data);
void LaneLeaksJob(FLeakJobData* Data);

} // namespace TraceServices
