// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "TraceServices/Model/NetProfiler.h"
#include "Styling/WidgetStyle.h"

enum class ESlateDrawEffect : uint8;

struct FDrawContext;
struct FGeometry;
struct FSlateBrush;

class FPacketContentViewport;
class FSlateWindowElementList;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketEvent
{
	uint32 EventTypeIndex;
	uint32 ObjectInstanceIndex;
	uint32 NetId;
	uint32 BitOffset;
	uint32 BitSize;
	uint32 Level;

	FNetworkPacketEvent()
		: EventTypeIndex(0)
		, ObjectInstanceIndex(0)
		, NetId(0)
		, BitOffset(0)
		, BitSize(0)
		, Level(0)
	{}

	FNetworkPacketEvent(uint32 InEventTypeIndex, uint32 InObjectInstanceIndex, uint32 InNetId, uint32 InBitOffset, uint32 InBitSize, uint32 InLevel)
		: EventTypeIndex(InEventTypeIndex)
		, ObjectInstanceIndex(InObjectInstanceIndex)
		, NetId(InNetId)
		, BitOffset(InBitOffset)
		, BitSize(InBitSize)
		, Level(InLevel)
	{}

	FNetworkPacketEvent(const FNetworkPacketEvent& Other)
		: EventTypeIndex(Other.EventTypeIndex)
		, ObjectInstanceIndex(Other.ObjectInstanceIndex)
		, NetId(Other.NetId)
		, BitOffset(Other.BitOffset)
		, BitSize(Other.BitSize)
		, Level(Other.Level)
	{
	}

	FNetworkPacketEvent& operator=(const FNetworkPacketEvent& Other)
	{
		EventTypeIndex = Other.EventTypeIndex;
		ObjectInstanceIndex = Other.ObjectInstanceIndex;
		NetId = Other.NetId;
		BitOffset = Other.BitOffset;
		BitSize = Other.BitSize;
		Level = Other.Level;
		return *this;
	}

	bool Equals(const FNetworkPacketEvent& Other) const
	{
		return EventTypeIndex == Other.EventTypeIndex
			&& ObjectInstanceIndex == Other.ObjectInstanceIndex
			&& NetId == Other.NetId
			&& BitOffset == Other.BitOffset
			&& BitSize == Other.BitSize
			&& Level == Other.Level;
	}

	static bool AreEquals(const FNetworkPacketEvent& A, const FNetworkPacketEvent& B)
	{
		return A.Equals(B);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FPacketContentViewDrawState
{
	struct FBox
	{
		float X;
		float Y;
		float W;
		float H;
		FLinearColor Color;
	};

	struct FText
	{
		float X;
		float Y;
		FString Text;
		bool bWhite;
	};

	FPacketContentViewDrawState()
	{
	}

	void Reset()
	{
		Events.Reset();
		Boxes.Reset();
		Borders.Reset();
		Texts.Reset();
		NumMergedBoxes = 0;
	}

	int32 GetNumMergedBoxes() const { return NumMergedBoxes; }

	TArray<FNetworkPacketEvent> Events;
	TArray<FBox> Boxes;
	TArray<FBox> Borders;
	TArray<FText> Texts;

	// Debug stats.
	int32 NumMergedBoxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketContentViewDrawStateBuilder
{
private:
	struct FBoxData
	{
		float X1;
		float X2;
		uint32 Color;
		FLinearColor LinearColor;

		FBoxData() : X1(0.0f), X2(0.0f), Color(0) {}
		void Reset() { X1 = 0.0f; X2 = 0.0f; Color = 0; }
	};

public:
	explicit FPacketContentViewDrawStateBuilder(FPacketContentViewDrawState& InState, const FPacketContentViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FPacketContentViewDrawStateBuilder(const FPacketContentViewDrawStateBuilder&) = delete;
	FPacketContentViewDrawStateBuilder& operator=(const FPacketContentViewDrawStateBuilder&) = delete;

	void AddEvent(const Trace::FNetProfilerContentEvent& Event, const TCHAR* Name, uint32 NetId);
	void Flush();

private:
	void FlushBox(const FBoxData& Box, const float EventY, const float EventH);

private:
	FPacketContentViewDrawState& DrawState; // cached draw state to build
	const FPacketContentViewport& Viewport;

	int32 MaxDepth;

	TArray<float> LastEventX2; // X2 value for last event on each depth
	TArray<FBoxData> LastBox;

	const FSlateFontInfo EventFont;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketContentViewDrawHelper
{
public:
	enum class EHighlightMode : uint32
	{
		Hovered = 1,
		Selected = 2,
		SelectedAndHovered = 3
	};

public:
	explicit FPacketContentViewDrawHelper(const FDrawContext& InDrawContext, const FPacketContentViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FPacketContentViewDrawHelper(const FPacketContentViewDrawHelper&) = delete;
	FPacketContentViewDrawHelper& operator=(const FPacketContentViewDrawHelper&) = delete;

	const FSlateBrush* GetWhiteBrush() const { return WhiteBrush; }
	const FSlateFontInfo& GetEventFont() const { return EventFont; }

	void DrawBackground() const;
	void Draw(const FPacketContentViewDrawState& DrawState) const;
	void DrawEventHighlight(const FNetworkPacketEvent& Event, EHighlightMode Mode) const;

	static FLinearColor GetColorByType(int32 Type);

private:
	const FDrawContext& DrawContext;
	const FPacketContentViewport& Viewport;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* EventBorderBrush;
	const FSlateBrush* HoveredEventBorderBrush;
	const FSlateBrush* SelectedEventBorderBrush;
	const FSlateFontInfo EventFont;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
