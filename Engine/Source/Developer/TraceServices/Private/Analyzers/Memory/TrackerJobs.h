// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Lane.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class alignas(16) FRetiree
	: public FAddressSerial
{
public:
	void			Set(uint32 Start, uint32 EndBiased, uint64 Address, uint32 MetadataId);
	uint32			GetStartSerial() const			{ return StartSerial; }
	uint32			GetEndSerial(uint32 Bias) const	{ return GetBiasedSerial() + Bias; }
	uint32			GetMetadataId() const			{ return Id; }

private:
	uint32			StartSerial;
	uint32			Id;
};

////////////////////////////////////////////////////////////////////////////////
inline void FRetiree::Set(uint32 Start, uint32 EndBiased, uint64 Address, uint32 MetadataId)
{
	FAddressSerial::Set(EndBiased, Address);
	StartSerial = Start;
	Id = MetadataId;
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
struct FRetireeJobData
{
	FRetirees*			Retirees;
	uint32				SerialBias;
	uint32				ColumnShift;
};

////////////////////////////////////////////////////////////////////////////////
struct FLaneJobData
	: public FRetireeJobData
{
	FLaneJobData*		Next;
	FLane*				Lane;
	FLaneInput*			Input;
	FLaneItemView		SetUpdates;
};

////////////////////////////////////////////////////////////////////////////////
struct FLeakJobData
	: public FRetireeJobData
{
	FLeakJobData*		Next;
	const FLaneItemSet* ActiveSet;
};

////////////////////////////////////////////////////////////////////////////////
void LaneRehashJob(void* Data);
void LaneInputJob(FLaneJobData* Data);
void LaneUpdateJob(FLaneJobData* Data);
void LaneRetireeJob(FRetireeJobData* Data);
void LaneLeaksJob(FLeakJobData* Data);

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
