// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "RenderGraphProvider.h"

class FMenuBuilder;
class ITimingTrackDrawContext;
class ITimingTrackUpdateContext;
class FTimingTrackViewport;

namespace UE
{
namespace RenderGraphInsights
{
class FGraphPacket;
class FPacketFilter;

class FVisibleItem
{
public:
	FVisibleItem(const FTimingTrackViewport& InViewport, const FPacket& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor, float InMinY, float InMaxY)
		: Packet(&InPacket)
		, Name(*InPacket.Name)
		, StartTime(InStartTime)
		, EndTime(InEndTime)
		, Depth(InDepth)
		, Color(InColor)
	{
		Min.X = InViewport.TimeToSlateUnitsRounded(StartTime);
		Min.Y = InMinY;
		Max.X = InViewport.TimeToSlateUnitsRounded(EndTime);
		Max.Y = InMaxY;
	}

	virtual bool Intersects(float InPosX, float InPosY, bool& bOutFilterable) const
	{
		bOutFilterable = true;
		return InPosX >= Min.X && InPosX < Max.X&& InPosY > Min.Y && InPosY < Max.Y;
	}

	bool Intersects(float InPosX, float InPosY) const
	{
		bool bFilterable = false;
		return Intersects(InPosX, InPosY, bFilterable);
	}

	const FPacket* Packet{};
	const TCHAR* Name{};
	double StartTime{};
	double EndTime{};
	uint32 Depth{};
	uint32 Color{};
	uint32 Index{};
	FVector2D Min = FVector2D::ZeroVector;
	FVector2D Max = FVector2D::ZeroVector;
};

template <typename InPacketType>
class TVisibleItemHelper : public FVisibleItem
{
public:
	using PacketType = InPacketType;

	TVisibleItemHelper(const FTimingTrackViewport& Viewport, const PacketType& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor)
		: TVisibleItemHelper(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor, Viewport.GetLayout().GetLaneY(InDepth))
	{}

	TVisibleItemHelper(const FTimingTrackViewport& Viewport, const PacketType& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor, float InMinY)
		: FVisibleItem(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor, InMinY, InMinY + Viewport.GetLayout().EventH)
	{}

	TVisibleItemHelper(const FTimingTrackViewport& Viewport, const PacketType& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor, float InMinY, float InMaxY)
		: FVisibleItem(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor, InMinY, InMaxY)
	{}

	const PacketType& GetPacket() const
	{
		return static_cast<const PacketType&>(*Packet);
	}
};

class FVisiblePass final : public TVisibleItemHelper<FPassPacket>
{
public:
	FVisiblePass(const FTimingTrackViewport& Viewport, const FPassPacket& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor)
		: TVisibleItemHelper(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor)
	{}

	bool Intersects(float InPosX, float InPosY, bool& bOutFilterable) const override
	{
		// For hit-testing, treat the pass as unbounded along Y, so that it's a column.
		if (InPosX >= Min.X && InPosX < Max.X && InPosY >= Min.Y)
		{
			bOutFilterable = InPosY < Max.Y;
			return true;
		}
		return false;
	}

	using FVisibleItem::Intersects;
};

class FVisibleScope final : public TVisibleItemHelper<FScopePacket>
{
public:

	FVisibleScope(const FTimingTrackViewport& Viewport, const FScopePacket& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor)
		: TVisibleItemHelper(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor)
	{}
};

class FVisibleTexture final : public TVisibleItemHelper<FTexturePacket>
{
public:

	FVisibleTexture(const FTimingTrackViewport& Viewport, const FTexturePacket& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor)
		: TVisibleItemHelper(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor)
	{}
};

class FVisibleBuffer final : public TVisibleItemHelper<FBufferPacket>
{
public:

	FVisibleBuffer(const FTimingTrackViewport& Viewport, const FBufferPacket& InPacket, double InStartTime, double InEndTime, uint32 InDepth, uint32 InColor)
		: TVisibleItemHelper(Viewport, InPacket, InStartTime, InEndTime, InDepth, InColor)
	{}
};

class FVisibleGraph final : public TVisibleItemHelper<FGraphPacket>
{
public:
	FVisibleGraph(const FTimingTrackViewport& Viewport, const FGraphPacket& InGraph, const uint32 InColor, float InMaxY)
		: TVisibleItemHelper(Viewport, InGraph, InGraph.StartTime, InGraph.EndTime, 0, InColor, 0.0f, InMaxY)
	{}

	void AddScope(const FVisibleScope& VisibleScope);
	void AddPass(const FVisiblePass& VisiblePass);
	void AddTexture(const FVisibleTexture& VisibleTexture);
	void AddBuffer(const FVisibleBuffer& VisibleBuffer);

	void Reset();

	const FVisibleItem* FindItem(float InPosX, float InPosY) const;

	const FVisibleScope& GetVisibleScope(const FScopePacket& Scope) const
	{
		return Scopes[Scope.VisibleIndex];
	}

	const FVisiblePass& GetVisiblePass(const FPassPacket& Pass) const
	{
		return Passes[Pass.VisibleIndex];
	}

	const FVisibleTexture& GetVisibleTexture(const FTexturePacket& Texture) const
	{
		return Textures[Texture.VisibleIndex];
	}

	const FVisibleBuffer& GetVisibleBuffer(const FBufferPacket& Buffer) const
	{
		return Buffers[Buffer.VisibleIndex];
	}

	TArray<FVisibleScope> Scopes;
	TArray<FVisiblePass> Passes;
	TArray<FVisibleTexture> Textures;
	TArray<FVisibleBuffer> Buffers;
	TArray<uint32> AsyncComputePasses;
};

class FRenderGraphTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FRenderGraphTrack, FTimingEventsTrack)
	using Super = FTimingEventsTrack;

public:
	FRenderGraphTrack(const FRenderGraphTimingViewSession& InSharedData);

	//~ Begin FTimingEventsTrack interface
	void Update(const ITimingTrackUpdateContext& Context) override;
	void Draw(const ITimingTrackDrawContext& Context) const override;
	void PostDraw(const ITimingTrackDrawContext& Context) const override;
	void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;
	TSharedPtr<ITimingEventFilter> GetFilterByEvent(const TSharedPtr<const ITimingEvent> InTimingEvent) const;
	void BuildContextMenu(FMenuBuilder& MenuBuilder) override;
	void InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const override;
	//~ End FTimingEventsTrack interface

private:
	const FRenderGraphTimingViewSession& SharedData;

	FVisibleGraph& AddVisibleGraph(const FTimingTrackViewport& Viewport, const FGraphPacket& Graph, uint32 Color)
	{
		const uint32 VisibleIndex = VisibleGraphs.Num();
		FVisibleGraph& VisibleGraph = VisibleGraphs.Emplace_GetRef(Viewport, Graph, Color, GetHeight());
		check(VisibleGraph.Packet->VisibleIndex == kInvalidVisibleIndex);
		VisibleGraph.Packet->VisibleIndex = VisibleIndex;
		VisibleGraph.Index = VisibleIndex;
		return VisibleGraph;
	}

	const FVisibleGraph* GetVisibleGraph(const FGraphPacket& Packet) const
	{
		if (Packet.VisibleIndex != kInvalidVisibleIndex)
		{
			return &VisibleGraphs[Packet.VisibleIndex];
		}
		return nullptr;
	}

	bool FilterPacket(const FPacket& Packet) const;
	bool FilterPacketExact(const FPacket& Packet) const;

	FVector2D MousePosition = FVector2D::ZeroVector;

	TArray<FVisibleGraph> VisibleGraphs;

	struct FSpline
	{
		float Thickness{};
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D StartDir = FVector2D::ZeroVector;
		FVector2D End = FVector2D::ZeroVector;
		FVector2D EndDir = FVector2D::ZeroVector;
		FLinearColor Tint = FLinearColor::White;
	};

	TArray<FSpline> Splines;

	const FPacketFilter* PacketFilter{};
	const FPassPacket* SelectedPass{};

	bool ShowTextures() const
	{
		return EnumHasAnyFlags(ResourceShow, EResourceShow::Textures);
	}

	bool ShowBuffers() const
	{
		return EnumHasAnyFlags(ResourceShow, EResourceShow::Buffers);
	}

	enum class EResourceShow
	{
		Textures = 1 << 0,
		Buffers  = 1 << 1,
		All = Textures | Buffers
	};
	FRIEND_ENUM_CLASS_FLAGS(FRenderGraphTrack::EResourceShow);

	static void AddEvent(ITimingEventsTrackDrawStateBuilder& Builder, const FVisibleItem& Item);

	EResourceShow ResourceShow = EResourceShow::All;

	FTooltipDrawState SelectedTooltipState;
};

ENUM_CLASS_FLAGS(FRenderGraphTrack::EResourceShow);

} //namespace RenderGraphInsights
} //namespace UE