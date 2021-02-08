// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGraphTrack.h"

#include "Modules/ModuleManager.h"
#include "RenderGraphProvider.h"
#include "RenderGraphTimingViewSession.h"

#include "Application/SlateApplicationBase.h"
#include "Fonts/FontMeasure.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "Insights/Common/PaintUtils.h"
#include "Insights/ViewModels/GraphSeries.h"
#include "Insights/ViewModels/GraphTrackBuilder.h"
#include "Insights/ViewModels/ITimingViewDrawHelper.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"

#include "TraceServices/Model/Frames.h"

#define LOCTEXT_NAMESPACE "RenderGraphTrack"

namespace UE
{
namespace RenderGraphInsights
{

class FEvent : public FTimingEvent
{
	INSIGHTS_DECLARE_RTTI(FEvent, FTimingEvent)
public:
	FEvent(const TSharedRef<const FBaseTimingTrack> InTrack, const FVisibleItem& InItem)
		: FTimingEvent(InTrack, InItem.Packet->StartTime, InItem.Packet->EndTime, InItem.Depth)
	{}

	virtual const FVisibleItem& GetItem() const = 0;
	virtual const FPacket& GetPacket() const = 0;
};

template <typename ItemType>
class TEventHelper : public FEvent
{
public:
	TEventHelper(const TSharedRef<const FBaseTimingTrack>& InTrack, const ItemType& InItem)
		: FEvent(InTrack, InItem)
		, Item(InItem)
	{
		Item.Index = kInvalidVisibleIndex;
		Item.StartTime = GetPacket().StartTime;
		Item.EndTime = GetPacket().EndTime;
	}

	const ItemType& GetItem() const override
	{
		return Item;
	}

	const typename ItemType::PacketType& GetPacket() const
	{
		return Item.GetPacket();
	}

private:
	ItemType Item;
};

class FVisibleScopeEvent final : public TEventHelper<FVisibleScope>
{
	INSIGHTS_DECLARE_RTTI(FVisibleScopeEvent, FEvent)
public:
	FVisibleScopeEvent(const TSharedRef<const FBaseTimingTrack>& InTrack, const FVisibleScope& InItem)
		: TEventHelper<FVisibleScope>(InTrack, InItem)
	{}
};

class FVisiblePassEvent final : public TEventHelper<FVisiblePass>
{
	INSIGHTS_DECLARE_RTTI(FVisiblePassEvent, FEvent)
public:
	FVisiblePassEvent(const TSharedRef<const FBaseTimingTrack>& InTrack, const FVisiblePass& InItem)
		: TEventHelper<FVisiblePass>(InTrack, InItem)
	{}
};

class FVisibleTextureEvent final : public TEventHelper<FVisibleTexture>
{
	INSIGHTS_DECLARE_RTTI(FVisibleTextureEvent, FEvent)
public:
	FVisibleTextureEvent(const TSharedRef<const FBaseTimingTrack>& InTrack, const FVisibleTexture& InItem)
		: TEventHelper<FVisibleTexture>(InTrack, InItem)
	{}
};

class FVisibleBufferEvent final : public TEventHelper<FVisibleBuffer>
{
	INSIGHTS_DECLARE_RTTI(FVisibleBufferEvent, FEvent)
public:
	FVisibleBufferEvent(const TSharedRef<const FBaseTimingTrack>& InTrack, const FVisibleBuffer& InItem)
		: TEventHelper<FVisibleBuffer>(InTrack, InItem)
	{}
};

class FPacketFilter final : public ITimingEventFilter
{
	INSIGHTS_DECLARE_RTTI(FPacketFilter, ITimingEventFilter)
public:
	FPacketFilter(const TSharedPtr<const FEvent>& InEvent)
		: Event(InEvent)
		, Packet(&InEvent->GetPacket())
		, Graph(Packet->Graph)
	{}
	virtual ~FPacketFilter() {}

	bool FilterPacket(const FPacket& Packet) const;
	bool FilterPacketExact(const FPacket& Packet) const;

	const FPacket& GetPacket() const
	{
		return *Packet;
	}

	const FGraphPacket& GetGraph() const
	{
		return *Graph;
	}

private:
	//! ITimingEventFilter
	bool FilterTrack(const FBaseTimingTrack& InTrack) const override { return true; }
	bool FilterEvent(const ITimingEvent& InEvent) const override { return true; }
	bool FilterEvent(double InEventStartTime, double InEventEndTime, uint32 InEventDepth, const TCHAR* InEventName, uint64 InEventType = 0, uint32 InEventColor = 0) const override { return true; }
	uint32 GetChangeNumber() const override { return 0; }
	//! ITimingEventFilter

	TSharedPtr<const FEvent> Event;
	const FPacket* Packet{};
	const FGraphPacket* Graph{};
};

INSIGHTS_IMPLEMENT_RTTI(FRenderGraphTrack)
INSIGHTS_IMPLEMENT_RTTI(FPacketFilter)
INSIGHTS_IMPLEMENT_RTTI(FEvent)
INSIGHTS_IMPLEMENT_RTTI(FVisibleScopeEvent)
INSIGHTS_IMPLEMENT_RTTI(FVisiblePassEvent)
INSIGHTS_IMPLEMENT_RTTI(FVisibleTextureEvent)
INSIGHTS_IMPLEMENT_RTTI(FVisibleBufferEvent)

template <typename VisibleItemType>
static VisibleItemType& AddVisibleItem(TArray<VisibleItemType>& VisibleItems, const VisibleItemType& InVisibleItem)
{
	const uint32 VisibleIndex = VisibleItems.Num();

	VisibleItemType& VisibleItem = VisibleItems.Emplace_GetRef(InVisibleItem);
	check(VisibleItem.Packet->VisibleIndex == kInvalidVisibleIndex);
	VisibleItem.Packet->VisibleIndex = VisibleIndex;
	VisibleItem.Index = VisibleIndex;
	return VisibleItem;
}

template <typename VisibleItemType>
static void ResetVisibleItemArray(TArray<VisibleItemType>& VisibleItems)
{
	for (VisibleItemType& VisibleItem : VisibleItems)
	{
		check(VisibleItem.Packet->VisibleIndex == VisibleItem.Index);
		VisibleItem.Packet->VisibleIndex = kInvalidVisibleIndex;
	}
	VisibleItems.Empty();
}

static bool Intersects(const FPassIntervalPacket& A, const FPassPacket& B)
{
	return A.FirstPass <= B.Handle && A.LastPass >= B.Handle;
}

static bool Intersects(const FPassIntervalPacket& A, const FPassIntervalPacket& B)
{
	return !(B.LastPass < A.FirstPass || A.LastPass < B.FirstPass);
}

static void AddEvent(ITimingEventsTrackDrawStateBuilder& Builder, const FVisibleItem& Item)
{
	Builder.AddEvent(Item.StartTime, Item.EndTime, Item.Depth, Item.Color, [&Item](float) { return Item.Name; });
};

bool FPacketFilter::FilterPacketExact(const FPacket& PacketToFilter) const
{
	return Packet == &PacketToFilter;
}

bool FPacketFilter::FilterPacket(const FPacket& PacketToFilter) const
{
	if (FilterPacketExact(PacketToFilter))
	{
		return true;
	}

	if (PacketToFilter.Graph != Graph)
	{
		return false;
	}

	if (Packet->Is<FScopePacket>())
	{
		const FScopePacket& Scope = Packet->As<FScopePacket>();

		if (PacketToFilter.Is<FPassIntervalPacket>())
		{
			const FPassIntervalPacket& PassIntervalToFilter = PacketToFilter.As<FPassIntervalPacket>();
			return Intersects(Scope, PassIntervalToFilter);
		}
		else if (PacketToFilter.Is<FPassPacket>())
		{
			const FPassPacket& PassToFilter = PacketToFilter.As<FPassPacket>();
			return Intersects(Scope, PassToFilter);
		}
	}

	return false;
}

bool FRenderGraphTrack::FilterPacket(const FPacket& Packet) const
{
	return PacketFilter ? PacketFilter->FilterPacket(Packet) : false;
}

bool FRenderGraphTrack::FilterPacketExact(const FPacket& Packet) const
{
	return PacketFilter ? PacketFilter->FilterPacketExact(Packet) : false;
}

void FVisibleGraph::Reset()
{
	ResetVisibleItemArray(Scopes);
	ResetVisibleItemArray(Passes);
	ResetVisibleItemArray(Textures);
	ResetVisibleItemArray(Buffers);
}

void FVisibleGraph::AddScope(const FVisibleScope& VisibleScope)
{
	AddVisibleItem(Scopes, VisibleScope);
}

void FVisibleGraph::AddPass(const FVisiblePass& InVisiblePass)
{
	const FVisiblePass& VisiblePass = AddVisibleItem(Passes, InVisiblePass);

	if (EnumHasAnyFlags(VisiblePass.GetPacket().Flags, ERDGPassFlags::AsyncCompute))
	{
		AsyncComputePasses.Add(VisiblePass.Index);
	}
}

void FVisibleGraph::AddTexture(const FVisibleTexture& VisibleTexture)
{
	AddVisibleItem(Textures, VisibleTexture);
}

void FVisibleGraph::AddBuffer(const FVisibleBuffer& VisibleBuffer)
{
	AddVisibleItem(Buffers, VisibleBuffer);
}

const FVisibleItem* FVisibleGraph::FindItem(float InPosX, float InPosY) const
{
	for (const FVisibleTexture& Texture : Textures)
	{
		if (Texture.Intersects(InPosX, InPosY))
		{
			return &Texture;
		}
	}

	for (const FVisibleBuffer& Buffer : Buffers)
	{
		if (Buffer.Intersects(InPosX, InPosY))
		{
			return &Buffer;
		}
	}

	for (const FVisibleScope& Scope : Scopes)
	{
		if (Scope.Intersects(InPosX, InPosY))
		{
			return &Scope;
		}
	}

	for (const FVisiblePass& Pass : Passes)
	{
		if (Pass.Intersects(InPosX, InPosY))
		{
			return &Pass;
		}
	}

	return nullptr;
}

FRenderGraphTrack::FRenderGraphTrack(const FRenderGraphTimingViewSession& InSharedData)
	: Super(LOCTEXT("TrackNameFormat", "RDG").ToString())
	, SharedData(InSharedData)
{}

static const uint32 kBuilderColor = 0xffa0a0a0;
static const uint32 kRasterPassColor = 0xff7F2D2D;
static const uint32 kComputePassColor = 0xff2D9F9F;
static const uint32 kNoParameterPassColor = 0xff4D4D4D;
static const uint32 kAsyncComputePassColor = 0xff2D7f2d;
static const uint32 kTextureColor = 0xff89cff0;
static const uint32 kBufferColor = 0xff66D066;

static const float kMinGraphPixels = 5.0f;
static const float kMinPassMarginPixels = 5.0f;

static uint32 GetPassColor(const FPassPacket& Packet)
{
	const ERDGPassFlags Flags = Packet.Flags;

	uint32 Color = 0;

	const bool bNoParameterPass = (!Packet.Buffers.Num() && !Packet.Textures.Num());

	if (bNoParameterPass && !Packet.bCulled)
	{
		Color = kNoParameterPassColor;
	}
	else if (EnumHasAnyFlags(Flags, ERDGPassFlags::AsyncCompute))
	{
		Color = kAsyncComputePassColor;
	}
	else if (EnumHasAnyFlags(Flags, ERDGPassFlags::Raster))
	{
		Color = kRasterPassColor;
	}
	else
	{
		Color = kComputePassColor;
	}

	if (Packet.bCulled)
	{
		Color &= 0x00FFFFFF;
		Color |= 0x40000000;
	}

	return Color;
}

static uint32 GetColorBySize(uint64 Size, uint64 MaxSize)
{
	const FLinearColor Low(0.01, 0.01, 0.01, 0.25f);
	const FLinearColor High(1.0, 0.1, 0.1, 1.0f);
	const float Percentage = FMath::Sqrt(float(Size) / float(MaxSize));
	return FLinearColor::LerpUsingHSV(Low, High, Percentage).ToFColor(false).ToPackedARGB();
}

uint32 FRenderGraphTrack::GetTextureColor(const FTexturePacket& Texture, uint64 MaxSizeInBytes) const
{
	if (ResourceColor == EResourceColor::Type)
	{
		return kTextureColor;
	}

	return GetColorBySize(Texture.SizeInBytes, MaxSizeInBytes);
}

uint32 FRenderGraphTrack::GetBufferColor(const FBufferPacket& Texture, uint64 MaxSizeInBytes) const
{
	if (ResourceColor == EResourceColor::Type)
	{
		return kBufferColor;
	}

	return GetColorBySize(Texture.SizeInBytes, MaxSizeInBytes);
}

void FRenderGraphTrack::Update(const ITimingTrackUpdateContext& Context)
{
	FTimingEventsTrack::Update(Context);

	MousePosition = Context.GetMousePosition();
	MousePosition.Y -= GetPosY();

	SelectedPass = nullptr;

	const TSharedPtr<const ITimingEvent> SelectedEventPtr = Context.GetSelectedEvent();
	if (SelectedEventPtr.IsValid() && SelectedEventPtr->Is<FEvent>())
	{
		if (SelectedEventPtr->Is<FVisiblePassEvent>())
		{
			SelectedPass = &SelectedEventPtr->As<FVisiblePassEvent>().GetPacket();
		}

		const FEvent& SelectedEvent = SelectedEventPtr->As<FEvent>();

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		const FVector2D ViewportPos(Viewport.GetWidth(), Viewport.GetHeight());

		InitTooltip(SelectedTooltipState, SelectedEvent);
		SelectedTooltipState.SetDesiredOpacity(0.75f);
		SelectedTooltipState.SetPosition(ViewportPos, 0.0f, Viewport.GetWidth(), 0.0f, Viewport.GetHeight());
		SelectedTooltipState.Update();
	}
	else
	{
		SelectedTooltipState.Reset();
	}
}

void FRenderGraphTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	FTimingEventsTrack::Draw(Context);

	const FEvent* HoveredEvent = nullptr;
	if (TSharedPtr<const ITimingEvent> HoveredEventBase = Context.GetHoveredEvent())
	{
		if (HoveredEventBase->Is<FEvent>())
		{
			HoveredEvent = &HoveredEventBase->As<FEvent>();
		}
	}

	const int32 LineLayerId = Context.GetHelper().GetFirstLayerId() - 1;
	const FTimingTrackViewport& Viewport = Context.GetViewport();
	const FDrawContext& DrawContext = Context.GetDrawContext();
	const ITimingViewDrawHelper& DrawHelper = Context.GetHelper();
	const FLinearColor EdgeColor = DrawHelper.GetEdgeColor();

	const float TrackY = GetPosY();
	const float TrackH = GetHeight();
	const float EventH = Viewport.GetLayout().EventH;
	const float ViewportWidth = Viewport.GetWidth();

	const auto DrawClampedBox = [&](int32 LayerId, float MinX, float MinY, float W, float H, const FLinearColor& Color)
	{
		const float Guardband = 1024.0f;
		float MaxX = MinX + W;

		if (MinX > ViewportWidth || MaxX < 0.0f)
		{
			return;
		}

		MinX = FMath::Max(MinX, -Guardband);
		MaxX = FMath::Min(MaxX, ViewportWidth + Guardband);
		W = MaxX - MinX;

		DrawContext.DrawBox(LayerId, MinX, MinY, W, H, DrawHelper.GetWhiteBrush(), Color);
	};

	const auto DrawClampedSpline = [&](int32 SplineLayerId, FSpline Spline)
	{
		const float Guardband = 1024.0f;
		float MinX = Spline.Start.X;
		float MaxX = Spline.Start.X + Spline.End.X;

		if (MinX > ViewportWidth || MaxX < 0.0f)
		{
			return;
		}

		MinX = FMath::Max(MinX, -Guardband);
		MaxX = FMath::Min(MaxX, ViewportWidth + Guardband);

		const float Width = MaxX - MinX;

		const float MinY = TrackY + Spline.Start.Y;

		Spline.Start.X = MinX;
		Spline.End.X = Width;
		Spline.StartDir.X = FMath::Min(Guardband, Spline.StartDir.X);
		Spline.EndDir.X = FMath::Min(Guardband, Spline.EndDir.X);

		DrawContext.DrawSpline(SplineLayerId, MinX, MinY, FVector2D::ZeroVector, Spline.StartDir, Spline.End, Spline.EndDir, Spline.Thickness, Spline.Tint);
	};

	if (TrackH > 0.0f)
	{
		const FTimingViewLayout& Layout = Viewport.GetLayout();

		for (const FVisibleGraph& VisibleGraph : VisibleGraphs)
		{
			const FGraphPacket& Graph = VisibleGraph.GetPacket();
			const uint32 PassCount = Graph.PassCount;
			const float LineStrideMin = 5.0f;
			const float GraphLineStride = VisibleGraph.Max.X - VisibleGraph.Min.X;

			if (GraphLineStride >= LineStrideMin)
			{
				DrawClampedBox(LineLayerId, VisibleGraph.Min.X, TrackY, 1.0f, TrackH, EdgeColor);
				DrawClampedBox(LineLayerId, VisibleGraph.Max.X, TrackY, 1.0f, TrackH, EdgeColor);
			}

			const float PassLineStride = GraphLineStride / float(PassCount);

			FVector2D RenderPassMergeMin = FVector2D::ZeroVector;

			for (uint32 PassIndex = 0; PassIndex < PassCount; ++PassIndex)
			{
				const FPassPacket& Pass = Graph.Passes[PassIndex];
				const FVisiblePass& VisiblePass = VisibleGraph.GetVisiblePass(Pass);

				if (!Pass.bSkipRenderPassBegin && Pass.bSkipRenderPassEnd)
				{
					RenderPassMergeMin = VisiblePass.Min;
				}

				if (Pass.bSkipRenderPassBegin && !Pass.bSkipRenderPassEnd)
				{
					const FVector2D RenderPassMergeMax = VisiblePass.Max;
					const float RenderPassMarginY = 3.0f;
					const float W = RenderPassMergeMax.X - RenderPassMergeMin.X;
					const float H = (RenderPassMergeMax.Y - RenderPassMergeMin.Y) * 0.25f;
					const float X = RenderPassMergeMin.X;
					const float Y = RenderPassMergeMin.Y - H - RenderPassMarginY;

					DrawClampedBox(LineLayerId, X, TrackY + Y, W, H, FLinearColor(0.8f, 0.2f, 0.2f, 0.75f));
				}

				const float X = Viewport.TimeToSlateUnitsRounded(Pass.StartTime);
				const float Y = TrackY + VisiblePass.Max.Y;
				const float H = TrackH - VisiblePass.Max.Y;

				const bool bHoveredPass = HoveredEvent && &HoveredEvent->GetPacket() == &Pass;
				const bool bSelectedPass = SelectedPass == &Pass;
				const bool bFilteredPass = FilterPacket(Pass);

				if (bHoveredPass || bFilteredPass || bSelectedPass)
				{
					const float W = PassLineStride + 1;

					const FLinearColor EdgeColorTranslucent = FLinearColor(EdgeColor.R, EdgeColor.G, EdgeColor.B, bFilteredPass ? 1.0f : 0.5f);

					DrawClampedBox(LineLayerId, X, Y, W, H, EdgeColorTranslucent);
				}
				else if (PassLineStride >= LineStrideMin && PassIndex != 0)
				{
					const float W = 1.0f;

					DrawClampedBox(LineLayerId, X, Y, W, H, EdgeColor);
				}
			}
		}
	}

	const int32 SplineOverLayerId = LineLayerId + Context.GetHelper().GetNumLayerIds();
	const int32 SplineUnderLineLayerId = LineLayerId + 1;

	for (const FSpline& Spline : Splines)
	{
		DrawClampedSpline(SplineOverLayerId, Spline);
	}

	const bool bFilterActive = Context.GetEventFilter() != nullptr;

	for (const FVisibleGraph& VisibleGraph : VisibleGraphs)
	{
		const FGraphPacket& Graph = VisibleGraph.GetPacket();

		for (uint32 VisibleIndex : VisibleGraph.AsyncComputePasses)
		{
			const FVisiblePass& AsyncComputeVisiblePass = VisibleGraph.Passes[VisibleIndex];
			const FPassPacket& AsyncComputePass = AsyncComputeVisiblePass.GetPacket();

			float TintAlpha = 0.25f;

			if (!bFilterActive || FilterPacket(AsyncComputePass))
			{
				TintAlpha = 0.75f;
			}

			const float StartT = 0.2f;
			const float EndT = 1.0f - StartT;
			const float SplineDir = 20.0f;

			const FRDGPassHandle GraphicsForkHandle = AsyncComputePass.GraphicsForkPass;

			if (GraphicsForkHandle.IsValid() && AsyncComputePass.bAsyncComputeBegin)
			{
				const FPassPacket& GraphicsForkPass = *Graph.GetPass(AsyncComputePass.GraphicsForkPass);
				const FVisiblePass& GraphicsForkVisiblePass = VisibleGraph.GetVisiblePass(GraphicsForkPass);

				const float X = FMath::Lerp(GraphicsForkVisiblePass.Min.X, GraphicsForkVisiblePass.Max.X, EndT);
				const float Y = GraphicsForkVisiblePass.Max.Y;
				const float LocalEndX = FMath::Lerp(AsyncComputeVisiblePass.Min.X, AsyncComputeVisiblePass.Max.X, StartT) - X;
				const float LocalEndY = AsyncComputeVisiblePass.Min.Y - Y;

				FSpline Spline;
				Spline.Start.X = X;
				Spline.Start.Y = Y;
				Spline.StartDir = FVector2D(0, SplineDir);
				Spline.End = FVector2D(LocalEndX, LocalEndY);
				Spline.EndDir = FVector2D(0, SplineDir);
				Spline.Thickness = 2.0f;
				Spline.Tint = FLinearColor(0.4f, 1.0f, 0.4f, TintAlpha);
				DrawClampedSpline(SplineUnderLineLayerId + 1, Spline);
			}

			const FRDGPassHandle GraphicsJoinHandle = AsyncComputePass.GraphicsJoinPass;

			if (GraphicsJoinHandle.IsValid() && AsyncComputePass.bAsyncComputeEnd)
			{
				const FPassPacket& GraphicsJoinPass = *Graph.GetPass(AsyncComputePass.GraphicsJoinPass);
				const FVisiblePass& GraphicsJoinVisiblePass = VisibleGraph.GetVisiblePass(GraphicsJoinPass);

				const float X = FMath::Lerp(AsyncComputeVisiblePass.Min.X, AsyncComputeVisiblePass.Max.X, EndT);
				const float Y = AsyncComputeVisiblePass.Min.Y;
				const float LocalEndX = FMath::Lerp(GraphicsJoinVisiblePass.Min.X, GraphicsJoinVisiblePass.Max.X, StartT) - X;
				const float LocalEndY = GraphicsJoinVisiblePass.Max.Y - Y;

				FSpline Spline;
				Spline.Start.X = X;
				Spline.Start.Y = Y;
				Spline.StartDir = FVector2D(0, -SplineDir);
				Spline.End = FVector2D(LocalEndX, LocalEndY);
				Spline.EndDir = FVector2D(0, -SplineDir);
				Spline.Thickness = 2.0f;
				Spline.Tint = FLinearColor(1.0f, 0.4f, 0.4f, TintAlpha);
				DrawClampedSpline(SplineUnderLineLayerId + 1, Spline);
			}
		}
	}
}

void FRenderGraphTrack::PostDraw(const ITimingTrackDrawContext& Context) const
{
	SelectedTooltipState.Draw(Context.GetDrawContext());
}

void FRenderGraphTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	// Reset draw state; it's built each frame.
	{
		Splines.Empty();
		for (FVisibleGraph& VisibleGraph : VisibleGraphs)
		{
			VisibleGraph.Reset();
		}
		ResetVisibleItemArray(VisibleGraphs);

		PacketFilter = nullptr;
		if (TSharedPtr<ITimingEventFilter> Filter = Context.GetEventFilter())
		{
			if (Filter->Is<FPacketFilter>())
			{
				PacketFilter = &Filter->As<FPacketFilter>();
			}
		}
	}

	if (const FRenderGraphProvider* RenderGraphProvider = SharedData.GetAnalysisSession().ReadProvider<FRenderGraphProvider>(FRenderGraphProvider::ProviderName))
	{
		struct FResourceEntry
		{
			double StartTime{};
			double EndTime{};
			uint64 SizeInBytes{};
			uint32 Index{};
			uint32 Order{};
			ERDGParentResourceType Type = ERDGParentResourceType::Texture;
			bool bHasPreviousOwner{};
		};

		TArray<FResourceEntry> Resources;
		TArray<uint16> TextureIndexToDepth;
		TArray<uint16> BufferIndexToDepth;

		const FTimingTrackViewport& Viewport = Context.GetViewport();

		TraceServices::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		const FRenderGraphProvider::TGraphTimeline& GraphTimeline = RenderGraphProvider->GetGraphTimeline();
		GraphTimeline.EnumerateEvents(Viewport.GetStartTime(), Viewport.GetEndTime(),
			[this, &Builder, &Viewport, &Resources, &TextureIndexToDepth, &BufferIndexToDepth](double GraphStartTime, double GraphEndTime, uint32, const TSharedPtr<FGraphPacket>& Graph)
		{
			// We always render the graph event, so add it separately even if the visible graph is culled.
			Builder.AddEvent(GraphStartTime, GraphEndTime, 0, *Graph->Name, 0, kBuilderColor);

			if (Viewport.GetViewportDXForDuration(GraphEndTime - GraphStartTime) <= kMinGraphPixels)
			{
				return TraceServices::EEventEnumerate::Continue;
			}

			FVisibleGraph& VisibleGraph = AddVisibleGraph(Viewport, *Graph, kBuilderColor);

			double SinglePixelTimeMargin = Viewport.GetDurationForViewportDX(1.0);

			if (Viewport.GetViewportDXForDuration(Graph->NormalizedPassDuration) <= kMinPassMarginPixels)
			{
				SinglePixelTimeMargin = 0.0;
			}

			uint32 DepthOffset = 1;

			for (uint32 ScopeIndex = 0, ScopeCount = Graph->Scopes.Num(); ScopeIndex < ScopeCount; ++ScopeIndex)
			{
				const FScopePacket& Scope = Graph->Scopes[ScopeIndex];
				const double StartTime = Scope.StartTime + SinglePixelTimeMargin;
				const double EndTime = Scope.EndTime;

				const FVisibleScope VisibleScope(Viewport, Scope, StartTime, EndTime, DepthOffset + Scope.Depth, FTimingEvent::ComputeEventColor(*Scope.Name));
				AddEvent(Builder, VisibleScope);
				VisibleGraph.AddScope(VisibleScope);
			}

			if (Graph->ScopeDepth)
			{
				DepthOffset += Graph->ScopeDepth + 1;
			}

			// +1 for render pass merge bars.
			DepthOffset += 1;

			bool bAnyAsyncCompute = false;

			for (uint32 PassIndex = 0, PassCount = Graph->Passes.Num(); PassIndex < PassCount; ++PassIndex)
			{
				FPassPacket& Pass = Graph->Passes[PassIndex];
				const double StartTime = Pass.StartTime + SinglePixelTimeMargin;
				const double EndTime = Pass.EndTime;
				const bool bAsyncCompute = EnumHasAnyFlags(Pass.Flags, ERDGPassFlags::AsyncCompute);
				const uint32 Depth = DepthOffset + (bAsyncCompute ? 2 : 0);
				const uint32 Color = GetPassColor(Pass);

				const FVisiblePass VisiblePass(Viewport, Pass, StartTime, EndTime, Depth, Color);
				AddEvent(Builder, VisiblePass);
				VisibleGraph.AddPass(VisiblePass);

				bAnyAsyncCompute |= bAsyncCompute;
			}

			// Empty space between passes / resources.
			DepthOffset += bAnyAsyncCompute ? 3 : 1;

			Resources.Reset();
			Resources.Reserve(Graph->Textures.Num() + Graph->Buffers.Num());
			TextureIndexToDepth.SetNum(Graph->Textures.Num());
			BufferIndexToDepth.SetNum(Graph->Buffers.Num());

			TBitArray<TInlineAllocator<8>> CulledTextures(true, Graph->Textures.Num());

			const auto CheckFilterName = [this](const FString& InName)
			{
				return FilterText.IsEmpty() || InName.Contains(FilterText);
			};

			const auto CheckFilterSize = [this](uint64 InSizeInBytes)
			{
				return FilterSize <= 0.0f || InSizeInBytes >= uint64(FilterSize * 1024.0f * 1024.0f);
			};

			if (ShowTextures())
			{
				for (uint32 TextureIndex = 0, TextureCount = Graph->Textures.Num(); TextureIndex < TextureCount; ++TextureIndex)
				{
					FTexturePacket& Texture = Graph->Textures[TextureIndex];

					const bool bCulled = Texture.bCulled || !CheckFilterName(Texture.Name) || !CheckFilterSize(Texture.SizeInBytes);
					CulledTextures[TextureIndex] = bCulled;

					if (bCulled)
					{
						continue;
					}

					FResourceEntry& Entry = Resources.AddDefaulted_GetRef();
					Entry.StartTime = Texture.StartTime;
					Entry.EndTime = Texture.EndTime;
					Entry.SizeInBytes = Texture.SizeInBytes;
					Entry.Index = TextureIndex;
					Entry.Order = Texture.Order;
					Entry.Type = ERDGParentResourceType::Texture;
					Entry.bHasPreviousOwner = Texture.PrevousOwnerHandle.IsValid();
				}
			}

			TBitArray<TInlineAllocator<8>> CulledBuffers(true, Graph->Buffers.Num());

			if (ShowBuffers())
			{
				for (uint32 BufferIndex = 0, BufferCount = Graph->Buffers.Num(); BufferIndex < BufferCount; ++BufferIndex)
				{
					FBufferPacket& Buffer = Graph->Buffers[BufferIndex];

					const bool bCulled = Buffer.bCulled || !CheckFilterName(Buffer.Name) || !CheckFilterSize(Buffer.SizeInBytes);
					CulledBuffers[BufferIndex] = bCulled;

					if (bCulled)
					{
						continue;
					}

					FResourceEntry& Entry = Resources.AddDefaulted_GetRef();
					Entry.StartTime = Buffer.StartTime;
					Entry.EndTime = Buffer.EndTime;
					Entry.SizeInBytes = Buffer.SizeInBytes;
					Entry.Index = BufferIndex;
					Entry.Order = Buffer.Order;
					Entry.Type = ERDGParentResourceType::Buffer;
					Entry.bHasPreviousOwner = Buffer.PrevousOwnerHandle.IsValid();
				}
			}

			switch (ResourceSort)
			{
			case EResourceSort::LargestSize:
				Resources.StableSort([](const FResourceEntry& LHS, const FResourceEntry& RHS)
				{
					return LHS.SizeInBytes > RHS.SizeInBytes;
				});
				break;
			case EResourceSort::SmallestSize:
				Resources.StableSort([](const FResourceEntry& LHS, const FResourceEntry& RHS)
				{
					return LHS.SizeInBytes < RHS.SizeInBytes;
				});
				break;
			case EResourceSort::StartOfLifetime:
				Resources.StableSort([](const FResourceEntry& LHS, const FResourceEntry& RHS)
				{
					return LHS.StartTime < RHS.StartTime;
				});
				break;
			case EResourceSort::EndOfLifetime:
				Resources.StableSort([](const FResourceEntry& LHS, const FResourceEntry& RHS)
				{
					return LHS.EndTime > RHS.EndTime;
				});
				break;
			default:
				Resources.StableSort([](const FResourceEntry& LHS, const FResourceEntry& RHS)
				{
					return LHS.Order < RHS.Order;
				});
				break;
			}

			uint64 MaxSizeInBytes = 0;

			for (FResourceEntry& Entry : Resources)
			{
				if (!Entry.bHasPreviousOwner)
				{
					auto& Array = Entry.Type == ERDGParentResourceType::Texture ? TextureIndexToDepth : BufferIndexToDepth;
					Array[Entry.Index] = DepthOffset++;
				}

				MaxSizeInBytes = FMath::Max(MaxSizeInBytes, Entry.SizeInBytes);
			}

			if (ShowTextures())
			{
				for (uint32 TextureIndex = 0, TextureCount = Graph->Textures.Num(); TextureIndex < TextureCount; ++TextureIndex)
				{
					if (CulledTextures[TextureIndex])
					{
						continue;
					}

					FTexturePacket& Texture = Graph->Textures[TextureIndex];

					if (Texture.PrevousOwnerHandle.IsValid())
					{
						TextureIndexToDepth[TextureIndex] = TextureIndexToDepth[Texture.PrevousOwnerHandle.GetIndex()];
					}

					const uint32 Depth = TextureIndexToDepth[TextureIndex];
					const double StartTime = Texture.StartTime + SinglePixelTimeMargin;
					const double EndTime = Texture.EndTime;
					const uint32 Color = GetTextureColor(Texture, MaxSizeInBytes);

					const FVisibleTexture VisibleTexture(Viewport, Texture, StartTime, EndTime, Depth, Color);
					AddEvent(Builder, VisibleTexture);
					VisibleGraph.AddTexture(VisibleTexture);
				}
			}

			if (ShowBuffers())
			{
				for (uint32 BufferIndex = 0, BufferCount = Graph->Buffers.Num(); BufferIndex < BufferCount; ++BufferIndex)
				{
					if (CulledBuffers[BufferIndex])
					{
						continue;
					}

					FBufferPacket& Buffer = Graph->Buffers[BufferIndex];

					if (Buffer.PrevousOwnerHandle.IsValid())
					{
						BufferIndexToDepth[BufferIndex] = BufferIndexToDepth[Buffer.PrevousOwnerHandle.GetIndex()];
					}

					const uint32 Depth = BufferIndexToDepth[BufferIndex];
					const double StartTime = Buffer.StartTime + SinglePixelTimeMargin;
					const double EndTime = Buffer.EndTime;
					const uint32 Color = GetBufferColor(Buffer, MaxSizeInBytes);

					const FVisibleBuffer VisibleBuffer(Viewport, Buffer, StartTime, EndTime, Depth, Color);
					AddEvent(Builder, VisibleBuffer);
					VisibleGraph.AddBuffer(VisibleBuffer);
				}
			}

			return TraceServices::EEventEnumerate::Continue;
		});
	}
}

void FRenderGraphTrack::BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	if (!PacketFilter)
	{
		return;
	}

	const FGraphPacket& Graph = PacketFilter->GetGraph();
	const FVisibleGraph* VisibleGraph = GetVisibleGraph(Graph);

	if (!VisibleGraph)
	{
		return;
	}

	AddEvent(Builder, *VisibleGraph);

	TSet<const FVisibleItem*> VisibleItems;

	for (const FVisibleScope& VisibleScope : VisibleGraph->Scopes)
	{
		if (!FilterPacket(VisibleScope.GetPacket()))
		{
			continue;
		}

		VisibleItems.Add(&VisibleScope);
	}

	for (const FVisiblePass& VisiblePass : VisibleGraph->Passes)
	{
		const FPassPacket& Pass = VisiblePass.GetPacket();

		if (!FilterPacket(Pass))
		{
			continue;
		}

		VisibleItems.Add(&VisiblePass);

		if (ShowTextures())
		{
			for (FRDGTextureHandle TextureHandle : Pass.Textures)
			{
				const FTexturePacket& Texture = *Graph.GetTexture(TextureHandle);
				if (const FVisibleTexture* VisibleTexture = VisibleGraph->GetVisibleTexture(Texture))
				{
					VisibleItems.Add(VisibleTexture);
				}
			}
		}

		if (ShowBuffers())
		{
			for (FRDGBufferHandle BufferHandle : Pass.Buffers)
			{
				const FBufferPacket& Buffer = *Graph.GetBuffer(BufferHandle);
				if (const FVisibleBuffer* VisibleBuffer = VisibleGraph->GetVisibleBuffer(Buffer))
				{
					VisibleItems.Add(VisibleBuffer);
				}
			}
		}

		for (const FVisibleScope& VisibleScope : VisibleGraph->Scopes)
		{
			const FScopePacket& Scope = VisibleScope.GetPacket();
			if (Intersects(Scope, Pass))
			{
				VisibleItems.Add(&VisibleScope);
			}
		}

		const auto AddFencePassEvent = [&](FRDGPassHandle InFencePassHandle)
		{
			const FPassPacket& GraphicsPass = *Graph.GetPass(InFencePassHandle);
			const FVisiblePass& GraphicsVisiblePass = VisibleGraph->GetVisiblePass(GraphicsPass);
			VisibleItems.Add(&GraphicsVisiblePass);
		};

		if (Pass.bAsyncComputeBegin)
		{
			AddFencePassEvent(Pass.GraphicsForkPass);
		}

		if (Pass.bAsyncComputeEnd)
		{
			AddFencePassEvent(Pass.GraphicsJoinPass);
		}
	}

	const auto AddResourcePassEvents = [&](const FVisibleItem& VisibleResource, TArrayView<const FRDGPassHandle> Passes)
	{
		AddEvent(Builder, VisibleResource);

		for (FRDGPassHandle PassHandle : Passes)
		{
			const FPassPacket& Pass = *Graph.GetPass(PassHandle);
			const FVisiblePass& VisiblePass = VisibleGraph->GetVisiblePass(Pass);
			AddEvent(Builder, VisiblePass);

			const float Y = VisiblePass.Max.Y;

			FSpline Spline;
			Spline.Start.X = (VisiblePass.Max.X + VisiblePass.Min.X) * 0.5f;
			Spline.Start.Y = Y;
			Spline.StartDir = FVector2D(0, -1);
			Spline.End = FVector2D(0, VisibleResource.Min.Y - Y);
			Spline.EndDir = FVector2D(0, 1);
			Spline.Thickness = 1.0f;
			Spline.Tint = FLinearColor(0.8f, 0.8f, 0.8f, 0.7f);
			Splines.Add(Spline);

			for (const FVisibleScope& VisibleScope : VisibleGraph->Scopes)
			{
				const FScopePacket& Scope = VisibleScope.GetPacket();
				if (Intersects(Scope, Pass))
				{
					VisibleItems.Add(&VisibleScope);
				}
			}
		}
	};

	if (ShowTextures())
	{
		for (const FVisibleTexture& VisibleTexture : VisibleGraph->Textures)
		{
			const FTexturePacket& Texture = VisibleTexture.GetPacket();

			if (FilterPacketExact(Texture))
			{
				AddResourcePassEvents(VisibleTexture, Texture.Passes);
			}
		}
	}

	if (ShowBuffers())
	{
		for (const FVisibleBuffer& VisibleBuffer : VisibleGraph->Buffers)
		{
			const FBufferPacket& Buffer = VisibleBuffer.GetPacket();

			if (FilterPacketExact(Buffer))
			{
				AddResourcePassEvents(VisibleBuffer, Buffer.Passes);
			}
		}
	}

	for (const FVisibleItem* VisibleItem : VisibleItems)
	{
		AddEvent(Builder, *VisibleItem);
	}
}

const TSharedPtr<const ITimingEvent> FRenderGraphTrack::GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const
{
	TSharedPtr<FEvent> Event;

	for (const FVisibleGraph& Graph : VisibleGraphs)
	{
		float AdjustedPosY = InPosY - GetPosY();

		if (Graph.Intersects(InPosX, AdjustedPosY))
		{
			if (const FVisibleItem* Item = Graph.FindItem(InPosX, AdjustedPosY))
			{
				const FPacket& Packet = *Item->Packet;

				if (Packet.Is<FScopePacket>())
				{
					Event = MakeShared<FVisibleScopeEvent>(SharedThis(this), *static_cast<const FVisibleScope*>(Item));
				}
				else if (Packet.Is<FPassPacket>())
				{
					Event = MakeShared<FVisiblePassEvent>(SharedThis(this), *static_cast<const FVisiblePass*>(Item));
				}
				else if (ShowTextures() && Packet.Is<FTexturePacket>())
				{
					Event = MakeShared<FVisibleTextureEvent>(SharedThis(this), *static_cast<const FVisibleTexture*>(Item));
				}
				else if (ShowBuffers() && Packet.Is<FBufferPacket>())
				{
					Event = MakeShared<FVisibleBufferEvent>(SharedThis(this), *static_cast<const FVisibleBuffer*>(Item));
				}
			}
		}
	}

	return Event;
}

TSharedPtr<ITimingEventFilter> FRenderGraphTrack::GetFilterByEvent(const TSharedPtr<const ITimingEvent> InTimingEvent) const
{
	if (InTimingEvent.IsValid() && InTimingEvent->Is<FEvent>())
	{
		bool bFilterable = false;
		if (InTimingEvent->As<FEvent>().GetItem().Intersects(MousePosition.X, MousePosition.Y, bFilterable) && bFilterable)
		{
			return MakeShared<FPacketFilter>(StaticCastSharedPtr<const FEvent>(InTimingEvent));
		}
	}
	return nullptr;
}

const TCHAR* GetDimensionName(ETextureDimension Dimension)
{
	switch (Dimension)
	{
	case ETextureDimension::Texture2D:
		return TEXT("Texture2D");
	case ETextureDimension::Texture2DArray:
		return TEXT("Texture2DArray");
	case ETextureDimension::Texture3D:
		return TEXT("Texture3D");
	case ETextureDimension::TextureCube:
		return TEXT("TextureCube");
	case ETextureDimension::TextureCubeArray:
		return TEXT("TextureCubeArray");
	}
	return TEXT("");
}

void FRenderGraphTrack::BuildContextMenu(FMenuBuilder& MenuBuilder)
{
	Super::BuildContextMenu(MenuBuilder);

	MenuBuilder.BeginSection("Show", LOCTEXT("ShowMenuHeader", "Track Show Flags"));
	{
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ShowAlls", "Show All"),
			LOCTEXT("ShowAlls_Tooltip", "Show All resources in the lifetime view."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceShow = EResourceShow::All;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceShow == EResourceShow::All; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ShowTextures", "Show Textures"),
			LOCTEXT("ShowTextures_Tooltip", "Show Texture resources in the lifetime view."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceShow = EResourceShow::Textures;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceShow == EResourceShow::Textures; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ShowBuffers", "Show Buffers"),
			LOCTEXT("ShowBuffers_Tooltip", "Show Buffer resources in the lifetime view."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceShow = EResourceShow::Buffers;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceShow == EResourceShow::Buffers; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();
	
	MenuBuilder.BeginSection("Sort", LOCTEXT("SortMenuHeader", "Track Sort By"));
	{
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("SortCreation", "Creation"),
			LOCTEXT("SortCreation_Tooltip", "Resources created earlier in the graph builder are ordered first."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceSort = EResourceSort::Creation;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceSort == EResourceSort::Creation; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("SortLargestSize", "Largest Size"),
			LOCTEXT("SortLargestSize_Tooltip", "Resources with larger allocations are ordered first."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceSort = EResourceSort::LargestSize;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceSort == EResourceSort::LargestSize; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("SortSmallestSize", "Smallest Size"),
			LOCTEXT("SortSmallestSize_Tooltip", "Resources with smaller allocations are ordered first."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceSort = EResourceSort::SmallestSize;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceSort == EResourceSort::SmallestSize; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("SortStartOfLifetime", "Start Of Lifetime"),
			LOCTEXT("SortStartOfLifetime_Tooltip", "Resources with earlier starting lifetimes are ordered first."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceSort = EResourceSort::StartOfLifetime;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceSort == EResourceSort::StartOfLifetime; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
		
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("SortEndOfLifetime", "End Of Lifetime"),
			LOCTEXT("SortEndOfLifetime_Tooltip", "Resources with later ending lifetimes are ordered first."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceSort = EResourceSort::EndOfLifetime;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceSort == EResourceSort::EndOfLifetime; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("Color", LOCTEXT("ColorMenuHeader", "Track Resource Coloration"));
	{
		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ColorType", "By Type"),
			LOCTEXT("ColorType_Tooltip", "Each type of resource has a unique color."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceColor = EResourceColor::Type;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceColor == EResourceColor::Type; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			LOCTEXT("ColorSize", "By Size"),
			LOCTEXT("ColorSize_Tooltip", "Larger resources are more brightly colored."),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					ResourceColor = EResourceColor::Size;
					SetDirtyFlag();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return ResourceColor == EResourceColor::Size; })
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("FilterText", LOCTEXT("FilterTextHeader", "Track Resource Filter"));
	{
		MenuBuilder.AddWidget(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f)
			[
				// Search box allows for filtering
				SNew(SSearchBox)
				.InitialText(FText::FromString(FilterText))
				.HintText(LOCTEXT("SearchHint", "Filter By Name"))
				.OnTextChanged_Lambda([this](const FText& InText) { FilterText = InText.ToString(); SetDirtyFlag(); })
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(5.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("SizeThreshold", "Filter By Size (MB)"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SSpinBox<float>)
					.MinValue(0)
					.MaxValue(1024)
					.Value(FilterSize)
					.MaxFractionalDigits(3)
					.MinDesiredWidth(60)
					.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type) { FilterSize = InValue; SetDirtyFlag(); })
				]
			],
			FText::GetEmpty(), true);
	}
	MenuBuilder.EndSection();
}

void FRenderGraphTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& InTooltipEvent) const
{
	Tooltip.ResetContent();

	if (InTooltipEvent.CheckTrack(this) && InTooltipEvent.Is<FEvent>())
	{
		const auto AddCommonResourceText = [&](const FResourcePacket& Resource)
		{
			if (Resource.bExtracted)
			{
				Tooltip.AddTextLine(TEXT("Extracted"), FLinearColor::Red);
			}

			if (Resource.bExternal)
			{
				Tooltip.AddTextLine(TEXT("External"), FLinearColor::Red);
			}
		};

		if (InTooltipEvent.Is<FVisibleScopeEvent>())
		{
			const auto& TooltipEvent = InTooltipEvent.As<FVisibleScopeEvent>();
			const FScopePacket& Scope = TooltipEvent.GetPacket();
			Tooltip.AddTitle(Scope.Name);

			const uint32 PassCount = Scope.LastPass.GetIndex() - Scope.FirstPass.GetIndex() + 1;
			Tooltip.AddNameValueTextLine(TEXT("Passes:"), FString::Printf(TEXT("%d"), PassCount));
		}
		else if (InTooltipEvent.Is<FVisiblePassEvent>())
		{
			const auto& TooltipEvent = InTooltipEvent.As<FVisiblePassEvent>();
			const FPassPacket& Pass = TooltipEvent.GetPacket();

			Tooltip.AddTitle(GetSanitizedName(Pass.Name));
			Tooltip.AddNameValueTextLine(TEXT("Handle:"), FString::Printf(TEXT("%d"), Pass.Handle.GetIndex()));

			if (Pass.bCulled)
			{
				Tooltip.AddTextLine(TEXT("Culled"), FLinearColor::Red);
			}
			else
			{
				Tooltip.AddNameValueTextLine(TEXT("Used Textures:"), FString::Printf(TEXT("%d"), Pass.Textures.Num()));
				Tooltip.AddNameValueTextLine(TEXT("Used Buffers:"), FString::Printf(TEXT("%d"), Pass.Buffers.Num()));
			}

			if (Pass.bSkipRenderPassBegin || Pass.bSkipRenderPassEnd)
			{
				Tooltip.AddTextLine(TEXT("Merged RenderPass"), FLinearColor::Red);
			}
		}
		else if (InTooltipEvent.Is<FVisibleTextureEvent>())
		{
			const auto& TooltipEvent = InTooltipEvent.As<FVisibleTextureEvent>();
			const FTexturePacket& Texture = TooltipEvent.GetPacket();

			Tooltip.AddTitle(Texture.Name);
			Tooltip.AddNameValueTextLine(TEXT("Dimension:"), GetDimensionName(Texture.Desc.Dimension));
			Tooltip.AddNameValueTextLine(TEXT("Create Flags:"), GetTextureCreateFlagsName(Texture.Desc.Flags));
			Tooltip.AddNameValueTextLine(TEXT("Format:"), UEnum::GetValueAsString(Texture.Desc.Format));
			Tooltip.AddNameValueTextLine(TEXT("Extent:"), FString::Printf(TEXT("%d, %d"), Texture.Desc.Extent.X, Texture.Desc.Extent.Y));
			Tooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), Texture.Desc.Depth));
			Tooltip.AddNameValueTextLine(TEXT("Mips:"), FString::Printf(TEXT("%d"), Texture.Desc.NumMips));
			Tooltip.AddNameValueTextLine(TEXT("Array Size:"), FString::Printf(TEXT("%d"), Texture.Desc.ArraySize));
			Tooltip.AddNameValueTextLine(TEXT("Samples:"), FString::Printf(TEXT("%d"), Texture.Desc.NumSamples));
			Tooltip.AddNameValueTextLine(TEXT("Used Passes:"), FString::Printf(TEXT("%d"), Texture.Passes.Num()));
			AddCommonResourceText(Texture);
		}
		else if (InTooltipEvent.Is<FVisibleBufferEvent>())
		{
			const auto& TooltipEvent = InTooltipEvent.As<FVisibleBufferEvent>();
			const FBufferPacket& Buffer = TooltipEvent.GetPacket();

			Tooltip.AddTitle(Buffer.Name);
			Tooltip.AddNameValueTextLine(TEXT("Usage Flags:"), GetBufferUsageFlagsName(Buffer.Desc.Usage));
			Tooltip.AddNameValueTextLine(TEXT("Bytes Per Element:"), FString::Printf(TEXT("%d"), Buffer.Desc.BytesPerElement));
			Tooltip.AddNameValueTextLine(TEXT("Elements:"), FString::Printf(TEXT("%d"), Buffer.Desc.NumElements));
			Tooltip.AddNameValueTextLine(TEXT("Used Passes:"), FString::Printf(TEXT("%d"), Buffer.Passes.Num()));
			AddCommonResourceText(Buffer);
		}

		Tooltip.UpdateLayout();
	}
}

} //namespace RenderGraphInsights
} //namespace UE

#undef LOCTEXT_NAMESPACE