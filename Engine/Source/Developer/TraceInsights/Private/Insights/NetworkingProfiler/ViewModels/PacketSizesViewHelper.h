// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Styling/WidgetStyle.h"

#include <limits>

enum class ESlateDrawEffect : uint8;

struct FDrawContext;
struct FGeometry;
struct FSlateBrush;

class FPacketSizesViewport;
class FSlateWindowElementList;

////////////////////////////////////////////////////////////////////////////////////////////////////

enum class ENetworkPacketStatus
{
	Unknown = 0,
	ConfirmedReceived, // Sent + Ack(Received)
	Sent, // Sent, no Ack event yet
	ConfirmedLost // Sent + Ack(Lost)
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacket
{
	int32 FrameIndex;
	int64 Size; // [bits]
	double TimeSent; // time when packet is sent
	double TimeAck; // time when ACK event is received (either confirmed received or lost)
	ENetworkPacketStatus Status;

	FNetworkPacket()
		: FrameIndex(-1)
		, Size(-1)
		, TimeSent(std::numeric_limits<double>::infinity())
		, TimeAck(std::numeric_limits<double>::infinity())
		, Status(ENetworkPacketStatus::Unknown)
	{}

	FNetworkPacket(const FNetworkPacket&) = default;
	FNetworkPacket& operator=(const FNetworkPacket&) = default;

	FNetworkPacket(FNetworkPacket&&) = default;
	FNetworkPacket& operator=(FNetworkPacket&&) = default;

	bool Equals(const FNetworkPacket& Other) const
	{
		return FrameIndex == Other.FrameIndex
			&& Size == Other.Size
			/*&& TimeSent == Other.TimeSent
			&& TimeAck == Other.TimeAck
			&& Status == Other.Status*/;
	}

	static bool AreEquals(const FNetworkPacket& A, const FNetworkPacket& B)
	{
		return A.Equals(B);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketAggregatedSample
{
	int32 NumPackets;

	double StartTime; // min TimeSent of all packets in this aggregated sample
	double EndTime; // max TimeSent of all packets in this aggregated sample

	/** Aggregated status for packets in this aggregated sample.
	 *    Unknown  --> all aggregated packets are unknown
	 *    Sent     --> all aggregated packets are sent; none is received or confirmed lost
	 *    Received --> at least one packet in the sample set is confirmed received and none are confirmed lost
	 *    Lost     --> at least one packet in the sample set is confirmed lost
	**/
	ENetworkPacketStatus AggregatedStatus;

	FNetworkPacket LargestPacket;

	FNetworkPacketAggregatedSample()
		: NumPackets(0)
		, StartTime(DBL_MAX)
		, EndTime(-DBL_MAX)
		, AggregatedStatus(ENetworkPacketStatus::Unknown)
		, LargestPacket()
	{}

	FNetworkPacketAggregatedSample(const FNetworkPacketAggregatedSample&) = default;
	FNetworkPacketAggregatedSample& operator=(const FNetworkPacketAggregatedSample&) = default;

	FNetworkPacketAggregatedSample(FNetworkPacketAggregatedSample&&) = default;
	FNetworkPacketAggregatedSample& operator=(FNetworkPacketAggregatedSample&&) = default;

	bool Equals(const FNetworkPacketAggregatedSample& Other) const
	{
		return NumPackets == Other.NumPackets
			&& StartTime == Other.StartTime
			&& EndTime == Other.EndTime
			&& AggregatedStatus == Other.AggregatedStatus
			&& LargestPacket.Equals(Other.LargestPacket);
	}

	static bool AreEquals(const FNetworkPacketAggregatedSample& A, const FNetworkPacketAggregatedSample& B)
	{
		return A.Equals(B);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketSeries
{
	int32 NumAggregatedPackets; // total number of packets aggregated in samples; i.e. sum of all Sample.NumPackets

	TArray<FNetworkPacketAggregatedSample> Samples;

	FNetworkPacketSeries()
		: NumAggregatedPackets(0)
		, Samples()
	{
	}

	void Reset()
	{
		NumAggregatedPackets = 0;
		Samples.Reset();
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FNetworkPacketSeriesBuilder
{
public:
	explicit FNetworkPacketSeriesBuilder(FNetworkPacketSeries& InSeries, const FPacketSizesViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FNetworkPacketSeriesBuilder(const FNetworkPacketSeriesBuilder&) = delete;
	FNetworkPacketSeriesBuilder& operator=(const FNetworkPacketSeriesBuilder&) = delete;

	void AddPacket(int32 FrameIndex, int64 Size, double TimeSent, double TimeAck, ENetworkPacketStatus Status);

	int32 GetNumAddedPackets() const { return NumAddedPackets; }

private:
	FNetworkPacketSeries& Series; // series to update
	const FPacketSizesViewport& Viewport;

	float SampleW; // width of a sample, in Slate units
	int32 PacketsPerSample; // number of packets in a sample
	int32 FirstFrameIndex; // index of first frame in first sample; can be negative
	int32 NumSamples; // total number of samples

	// Debug stats.
	int32 NumAddedPackets; // counts total number of added packets; i.e. number of AddPacket() calls
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketSizesViewDrawHelper
{
public:
	enum class EHighlightMode : uint32
	{
		Hovered = 1,
		Selected = 2,
		SelectedAndHovered = 3
	};

public:
	explicit FPacketSizesViewDrawHelper(const FDrawContext& InDrawContext, const FPacketSizesViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FPacketSizesViewDrawHelper(const FPacketSizesViewDrawHelper&) = delete;
	FPacketSizesViewDrawHelper& operator=(const FPacketSizesViewDrawHelper&) = delete;

	void DrawBackground() const;
	void DrawCached(const FNetworkPacketSeries& Series) const;
	void DrawSampleHighlight(const FNetworkPacketAggregatedSample& Sample, EHighlightMode Mode) const;

	static FLinearColor GetColorByStatus(ENetworkPacketStatus Status);

	int32 GetNumPackets() const { return NumPackets; }
	int32 GetNumDrawSamples() const { return NumDrawSamples; }

private:
	const FDrawContext& DrawContext;
	const FPacketSizesViewport& Viewport;

	const FSlateBrush* WhiteBrush;
	//const FSlateBrush* EventBorderBrush;
	const FSlateBrush* HoveredEventBorderBrush;
	const FSlateBrush* SelectedEventBorderBrush;

	// Debug stats.
	mutable int32 NumPackets;
	mutable int32 NumDrawSamples;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
