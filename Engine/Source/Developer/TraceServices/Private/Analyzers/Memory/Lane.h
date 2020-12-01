// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/ArrayView.h"
#include "LaneItemSet.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class FAddressSerial
{
public:
	void Set(uint32 Serial, uint64 Address)
	{
		Address_Serial = Serial & (SerialMax - 1);
		Address_Serial += Address << UnusedHighBits;
	}

	uint64	GetAddress() const		{ return (Address_Serial >> UnusedHighBits) & ~AlignBits; }
	uint32	GetBiasedSerial() const	{ return Address_Serial & (SerialMax - 1); }

protected:
	enum
	{
		AlignBits		= 3,
		AddressBits		= 47,
		UnusedHighBits	= 64 - 47,
		SerialBits		= UnusedHighBits + AlignBits,
		SerialMax		= 1 << SerialBits,
	};

	uint64		Address_Serial; // packed for sorting purposes
};



////////////////////////////////////////////////////////////////////////////////
class alignas(16) FLaneItem
	: public FAddressSerial
{
public:
	void Set(uint32 Serial, uint64 Address, uint32 MetadataId)
	{
		FAddressSerial::Set(Serial, Address);
		Id = MetadataId;
	}

	uint32	GetSerial(uint32 Bias) const				{ return GetBiasedSerial() + Bias; }
	void	SetActiveIndex(uint32 Index)				{ ActiveIndex = Index; }
	uint32	GetActiveIndex() const						{ return ActiveIndex; }
	bool	operator < (const FLaneItem& Rhs) const		{ return Address_Serial < Rhs.Address_Serial; }
	bool	IsSameAddress(const FLaneItem& Rhs) const	{ return (Address_Serial ^ Rhs.Address_Serial) < SerialMax; }
	uint32	GetMetadataId() const						{ return Id; }
	bool	HasMetadata() const							{ return Id != 0; }

private:
	uint32		Id;
	uint32		ActiveIndex;
};
static_assert(sizeof(FLaneItem) == 16, "Deliberately XMM sized");



////////////////////////////////////////////////////////////////////////////////
typedef TArrayView<FLaneItem> FLaneItemView;



////////////////////////////////////////////////////////////////////////////////
class FLaneInput
{
public:
	bool				AddAlloc(uint64 Address, uint32 Serial, uint32 MetadataId);
	bool				AddFree(uint64 Address, uint32 Serial);
	uint32				GetNum() const	{ return Num; }
	FLaneItemView		GetAllocs()		{ return FLaneItemView(Items, NumAllocs); }
	FLaneItemView		GetFrees()		{ return FLaneItemView(Items + Max - NumFrees, NumFrees); }

private:
	uint32				Max;
	uint32				NumAllocs = 0;
	uint32				NumFrees = 0;
	uint32				Num = 0;
	FLaneItem			Items[];

	static_assert(sizeof(FLaneItem) == 16, "Deliberately XMM-sized");
	friend class FTrackerBuffer;
};

////////////////////////////////////////////////////////////////////////////////
inline bool FLaneInput::AddAlloc(uint64 Address, uint32 Serial, uint32 MetadataId)
{
	FLaneItem* Item = Items + NumAllocs;
	Item->Set(Serial, Address, MetadataId);

	++NumAllocs;
	++Num;
	return Num >= Max;
}

////////////////////////////////////////////////////////////////////////////////
inline bool FLaneInput::AddFree(uint64 Address, uint32 Serial)
{
	FLaneItem* Item = Items + Max - NumFrees - 1;
	Item->Set(Serial, Address, 0);

	++NumFrees;
	++Num;
	return Num >= Max;
}



////////////////////////////////////////////////////////////////////////////////
class FLane
{
public:
	void				LockWrite()				{}
	void				UnlockWrite()			{}
	FLaneItemSet&		GetActiveSet()			{ return ActiveSet; }
	const FLaneItemSet& GetActiveSet() const	{ return ActiveSet; }

private:
	FLaneItemSet		ActiveSet;
};

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
