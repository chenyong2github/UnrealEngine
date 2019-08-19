// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/WidgetStyle.h"

#include <limits>

enum class ESlateDrawEffect : uint8;

struct FDrawContext;
struct FGeometry;
struct FSlateBrush;

class FPacketBreakdownViewport;
class FSlateWindowElementList;

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FNetworkPacketEvent
{
	int64 Offset;
	int64 Size;
	int32 Type;
	int32 Depth;

	FNetworkPacketEvent(int64 InOffset, int64 InSize, int32 InType, int32 InDepth)
		: Offset(InOffset), Size(InSize), Type(InType), Depth(InDepth)
	{}

	FNetworkPacketEvent(const FNetworkPacketEvent& Other)
		: Offset(Other.Offset), Size(Other.Size), Type(Other.Type), Depth(Other.Depth)
	{
	}

	FNetworkPacketEvent& operator=(const FNetworkPacketEvent& Other)
	{
		Offset = Other.Offset;
		Size = Other.Size;
		Type = Other.Type;
		Depth = Other.Depth;
		return *this;
	}

	bool Equals(const FNetworkPacketEvent& Other) const
	{
		return Offset == Other.Offset && Size == Other.Size && Type == Other.Type && Depth == Other.Depth;
	}

	static bool AreEquals(const FNetworkPacketEvent& A, const FNetworkPacketEvent& B)
	{
		return A.Equals(B);
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FPacketBreakdownViewDrawState
{
	struct FPacketEvent
	{
		int64 Offset; // [bit]
		int64 Size; // [bit]
		int32 Type;
		int32 Depth;
	};

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

	FPacketBreakdownViewDrawState()
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

	TArray<FPacketEvent> Events;
	TArray<FBox> Boxes;
	TArray<FBox> Borders;
	TArray<FText> Texts;

	// Debug stats.
	int32 NumMergedBoxes;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketBreakdownViewDrawStateBuilder
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
	explicit FPacketBreakdownViewDrawStateBuilder(FPacketBreakdownViewDrawState& InState, const FPacketBreakdownViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FPacketBreakdownViewDrawStateBuilder(const FPacketBreakdownViewDrawStateBuilder&) = delete;
	FPacketBreakdownViewDrawStateBuilder& operator=(const FPacketBreakdownViewDrawStateBuilder&) = delete;

	void AddEvent(int64 Offset, int64 Size, int32 Type, int32 Depth);
	void Flush();

private:
	void FlushBox(const FBoxData& Box, const float EventY, const float EventH);

private:
	FPacketBreakdownViewDrawState& DrawState; // cached draw state to build
	const FPacketBreakdownViewport& Viewport;

	int32 MaxDepth;

	TArray<float> LastEventX2; // X2 value for last event on each depth
	TArray<FBoxData> LastBox;

	const FSlateFontInfo EventFont;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FPacketBreakdownViewDrawHelper
{
public:
	enum class EHighlightMode : uint32
	{
		Hovered = 1,
		Selected = 2,
		SelectedAndHovered = 3
	};

public:
	explicit FPacketBreakdownViewDrawHelper(const FDrawContext& InDrawContext, const FPacketBreakdownViewport& InViewport);

	/**
	 * Non-copyable
	 */
	FPacketBreakdownViewDrawHelper(const FPacketBreakdownViewDrawHelper&) = delete;
	FPacketBreakdownViewDrawHelper& operator=(const FPacketBreakdownViewDrawHelper&) = delete;

	const FSlateBrush* GetWhiteBrush() const { return WhiteBrush; }
	const FSlateFontInfo& GetEventFont() const { return EventFont; }

	void DrawBackground() const;
	void Draw(const FPacketBreakdownViewDrawState& DrawState) const;
	void DrawEventHighlight(const FNetworkPacketEvent& Event) const;

	static FLinearColor GetColorByType(int32 Type);

private:
	const FDrawContext& DrawContext;
	const FPacketBreakdownViewport& Viewport;

	const FSlateBrush* WhiteBrush;
	const FSlateBrush* EventBorderBrush;
	const FSlateBrush* HoveredEventBorderBrush;
	const FSlateBrush* SelectedEventBorderBrush;
	const FSlateFontInfo EventFont;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
