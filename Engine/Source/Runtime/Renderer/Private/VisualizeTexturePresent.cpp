// Copyright Epic Games, Inc. All Rights Reserved.

#include "VisualizeTexturePresent.h"
#include "VisualizeTexture.h"
#include "ScreenPass.h"
#include "UnrealEngine.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "RenderTargetPool.h"

// draw a single pixel sized rectangle using 4 sub elements
static void DrawBorder(FCanvas& Canvas, const FIntRect Rect, FLinearColor Color)
{
	// top
	Canvas.DrawTile(Rect.Min.X, Rect.Min.Y, Rect.Max.X - Rect.Min.X, 1, 0, 0, 1, 1, Color);
	// bottom
	Canvas.DrawTile(Rect.Min.X, Rect.Max.Y - 1, Rect.Max.X - Rect.Min.X, 1, 0, 0, 1, 1, Color);
	// left
	Canvas.DrawTile(Rect.Min.X, Rect.Min.Y + 1, 1, Rect.Max.Y - Rect.Min.Y - 2, 0, 0, 1, 1, Color);
	// right
	Canvas.DrawTile(Rect.Max.X - 1, Rect.Min.Y + 1, 1, Rect.Max.Y - Rect.Min.Y - 2, 0, 0, 1, 1, Color);
}

// helper class to get a consistent layout in multiple functions
// MaxX and Y are the output value that can be requested during or after iteration
// Examples usages:
//    FRenderTargetPoolEventIterator It(RenderTargetPoolEvents, OptionalStartIndex);
//    while(FRenderTargetPoolEvent* Event = It.Iterate()) {}
struct FRenderTargetPoolEventIterator
{
	int32 Index;
	TArray<FRenderTargetPoolEvent>& RenderTargetPoolEvents;
	bool bLineContent;
	uint32 TotalWidth;
	int32 Y;

	// constructor
	FRenderTargetPoolEventIterator(TArray<FRenderTargetPoolEvent>& InRenderTargetPoolEvents, int32 InIndex = 0)
		: Index(InIndex)
		, RenderTargetPoolEvents(InRenderTargetPoolEvents)
		, bLineContent(false)
		, TotalWidth(1)
		, Y(0)
	{
		Touch();
	}

	FRenderTargetPoolEvent* operator*()
	{
		if (Index < RenderTargetPoolEvents.Num())
		{
			return &RenderTargetPoolEvents[Index];
		}

		return 0;
	}

	// @return 0 if end was reached
	FRenderTargetPoolEventIterator& operator++()
	{
		if (Index < RenderTargetPoolEvents.Num())
		{
			++Index;
		}

		Touch();

		return *this;
	}

	int32 FindClosingEventY() const
	{
		FRenderTargetPoolEventIterator It = *this;

		const ERenderTargetPoolEventType StartType = (*It)->GetEventType();

		if (StartType == ERTPE_Alloc)
		{
			int32 PoolEntryId = RenderTargetPoolEvents[Index].GetPoolEntryId();

			++It;

			// search for next Dealloc of the same PoolEntryId
			for (; *It; ++It)
			{
				FRenderTargetPoolEvent* Event = *It;

				if (Event->GetEventType() == ERTPE_Dealloc && Event->GetPoolEntryId() == PoolEntryId)
				{
					break;
				}
			}
		}
		else if (StartType == ERTPE_Phase)
		{
			++It;

			// search for next Phase
			for (; *It; ++It)
			{
				FRenderTargetPoolEvent* Event = *It;

				if (Event->GetEventType() == ERTPE_Phase)
				{
					break;
				}
			}
		}
		else
		{
			check(0);
		}

		return It.Y;
	}

private:

	void Touch()
	{
		if (Index < RenderTargetPoolEvents.Num())
		{
			const FRenderTargetPoolEvent& Event = RenderTargetPoolEvents[Index];

			const ERenderTargetPoolEventType Type = Event.GetEventType();

			if (Type == ERTPE_Alloc)
			{
				// for now they are all equal width
				TotalWidth = FMath::Max(TotalWidth, Event.GetColumnX() + Event.GetColumnSize());
			}
			Y = Event.GetTimeStep();
		}
	}
};

uint32 FVisualizeTexturePresent::ComputeEventDisplayHeight()
{
	FRenderTargetPoolEventIterator It(GRenderTargetPool.RenderTargetPoolEvents);
	while (*It)
	{
		++It;
	}
	return It.Y;
}

void FVisualizeTexturePresent::OnStartRender(const FViewInfo& View)
{
#if SUPPORTS_VISUALIZE_TEXTURE
	GVisualizeTexture.FeatureLevel = View.GetFeatureLevel();
	GVisualizeTexture.Captured = {};
	GVisualizeTexture.VersionCountMap.Empty();
#endif
}

void FVisualizeTexturePresent::PresentContent(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassRenderTarget Output)
{
	check(Output.IsValid());

	if (GRenderTargetPool.RenderTargetPoolEvents.Num())
	{
		FIntPoint DisplayLeftTop(20, 50);
		// on the right we leave more space to make the mouse tooltip readable
		FIntPoint DisplayExtent(View.ViewRect.Width() - DisplayLeftTop.X * 2 - 140, View.ViewRect.Height() - DisplayLeftTop.Y * 2);

		// if the area is not too small
		if (DisplayExtent.X > 50 && DisplayExtent.Y > 50)
		{
			AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("RenderTargetPool"), View, Output, [&View, DisplayLeftTop, DisplayExtent] (FCanvas& Canvas)
			{
				FRenderTargetPool::SMemoryStats MemoryStats = GRenderTargetPool.ComputeView();

				// TinyFont property
				const int32 FontHeight = 12;

				FIntPoint MousePos = View.CursorPos;

				FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.7f);
				FLinearColor PhaseColor = FLinearColor(0.2f, 0.1f, 0.05f, 0.8f);
				FLinearColor ElementColor = FLinearColor(0.3f, 0.3f, 0.3f, 0.9f);
				FLinearColor ElementColorVRam = FLinearColor(0.4f, 0.25f, 0.25f, 0.9f);

				UTexture2D* GradientTexture = UCanvas::StaticClass()->GetDefaultObject<UCanvas>()->GradientTexture0;

				// background rectangle
				Canvas.DrawTile(DisplayLeftTop.X, DisplayLeftTop.Y - 1 * FontHeight - 1, DisplayExtent.X, DisplayExtent.Y + FontHeight, 0, 0, 1, 1, BackgroundColor);

				{
					uint32 MB = 1024 * 1024;
					uint32 MBm1 = MB - 1;

					FString Headline = *FString::Printf(TEXT("RenderTargetPool elements(x) over time(y) >= %dKB, Displayed/Total:%d/%dMB"),
						GRenderTargetPool.EventRecordingSizeThreshold,
						(uint32)((MemoryStats.DisplayedUsageInBytes + MBm1) / MB),
						(uint32)((MemoryStats.TotalUsageInBytes + MBm1) / MB));
					Canvas.DrawShadowedString(DisplayLeftTop.X, DisplayLeftTop.Y - 1 * FontHeight - 1, *Headline, GEngine->GetTinyFont(), FLinearColor(1, 1, 1));
				}

				uint32 EventDisplayHeight = FVisualizeTexturePresent::ComputeEventDisplayHeight();

				float ScaleX = DisplayExtent.X / (float)MemoryStats.TotalColumnSize;
				float ScaleY = DisplayExtent.Y / (float)EventDisplayHeight;

				// 0 if none
				FRenderTargetPoolEvent* HighlightedEvent = 0;
				FIntRect HighlightedRect;

				// Phase events
				for (FRenderTargetPoolEventIterator It(GRenderTargetPool.RenderTargetPoolEvents); *It; ++It)
				{
					FRenderTargetPoolEvent* Event = *It;

					if (Event->GetEventType() == ERTPE_Phase)
					{
						int32 Y0 = It.Y;
						int32 Y1 = It.FindClosingEventY();

						FIntPoint PixelLeftTop((int32)(DisplayLeftTop.X), (int32)(DisplayLeftTop.Y + ScaleY * Y0));
						FIntPoint PixelRightBottom((int32)(DisplayLeftTop.X + DisplayExtent.X), (int32)(DisplayLeftTop.Y + ScaleY * Y1));

						bool bHighlight = MousePos.X >= PixelLeftTop.X && MousePos.X < PixelRightBottom.X && MousePos.Y >= PixelLeftTop.Y && MousePos.Y <= PixelRightBottom.Y;

						if (bHighlight)
						{
							HighlightedEvent = Event;
							HighlightedRect = FIntRect(PixelLeftTop, PixelRightBottom);
						}

						// UMax is 0.9f to avoid getting some wrap texture leaking in at the bottom
						Canvas.DrawTile(PixelLeftTop.X, PixelLeftTop.Y, PixelRightBottom.X - PixelLeftTop.X, PixelRightBottom.Y - PixelLeftTop.Y, 0, 0, 1, 0.9f, PhaseColor, GradientTexture->Resource);
					}
				}

				// Alloc / Dealloc events
				for (FRenderTargetPoolEventIterator It(GRenderTargetPool.RenderTargetPoolEvents); *It; ++It)
				{
					FRenderTargetPoolEvent* Event = *It;

					if (Event->GetEventType() == ERTPE_Alloc && Event->GetColumnSize())
					{
						int32 Y0 = It.Y;
						int32 Y1 = It.FindClosingEventY();

						int32 X0 = Event->GetColumnX();
						// for now they are all equal width
						int32 X1 = X0 + Event->GetColumnSize();

						FIntPoint PixelLeftTop((int32)(DisplayLeftTop.X + ScaleX * X0), (int32)(DisplayLeftTop.Y + ScaleY * Y0));
						FIntPoint PixelRightBottom((int32)(DisplayLeftTop.X + ScaleX * X1), (int32)(DisplayLeftTop.Y + ScaleY * Y1));

						bool bHighlight = MousePos.X >= PixelLeftTop.X && MousePos.X < PixelRightBottom.X && MousePos.Y >= PixelLeftTop.Y && MousePos.Y <= PixelRightBottom.Y;

						if (bHighlight)
						{
							HighlightedEvent = Event;
							HighlightedRect = FIntRect(PixelLeftTop, PixelRightBottom);
						}

						FLinearColor Color = ElementColor;

						// Highlight EDRAM/FastVRAM usage
						if (Event->GetDesc().Flags & TexCreate_FastVRAM)
						{
							Color = ElementColorVRam;
						}

						Canvas.DrawTile(
							PixelLeftTop.X, PixelLeftTop.Y,
							PixelRightBottom.X - PixelLeftTop.X - 1, PixelRightBottom.Y - PixelLeftTop.Y - 1,
							0, 0, 1, 1, Color);
					}
				}

				if (HighlightedEvent)
				{
					DrawBorder(Canvas, HighlightedRect, FLinearColor(0.8f, 0, 0, 0.5f));

					// Offset to not intersect with crosshair (in editor) or arrow (in game).
					FIntPoint Pos = MousePos + FIntPoint(12, 4);

					if (HighlightedEvent->GetEventType() == ERTPE_Phase)
					{
						FString PhaseText = *FString::Printf(TEXT("Phase: %s"), *HighlightedEvent->GetPhaseName());

						Canvas.DrawShadowedString(Pos.X, Pos.Y + 0 * FontHeight, *PhaseText, GEngine->GetTinyFont(), FLinearColor(0.5f, 0.5f, 1));
					}
					else
					{
						FString SizeString = FString::Printf(TEXT("%d KB"), (HighlightedEvent->GetSizeInBytes() + 1024) / 1024);

						Canvas.DrawShadowedString(Pos.X, Pos.Y + 0 * FontHeight, HighlightedEvent->GetDesc().DebugName, GEngine->GetTinyFont(), FLinearColor(1, 1, 0));
						Canvas.DrawShadowedString(Pos.X, Pos.Y + 1 * FontHeight, *HighlightedEvent->GetDesc().GenerateInfoString(), GEngine->GetTinyFont(), FLinearColor(1, 1, 0));
						Canvas.DrawShadowedString(Pos.X, Pos.Y + 2 * FontHeight, *SizeString, GEngine->GetTinyFont(), FLinearColor(1, 1, 0));
					}
				}

				GRenderTargetPool.CurrentEventRecordingTime = 0;
				GRenderTargetPool.RenderTargetPoolEvents.Empty();
			});
		}
	}

#if SUPPORTS_VISUALIZE_TEXTURE
	FVisualizeTexture::FCaptured& Captured = GVisualizeTexture.Captured;

	if (!Captured.PooledRenderTarget && !Captured.Texture)
	{
		// visualize feature is deactivated
		return;
	}

	// Reset bitmap flags now that we know we've saved out the bitmap we're seeing on screen.
	{
		using EFlags = FVisualizeTexture::EFlags;
		EnumRemoveFlags(GVisualizeTexture.Config.Flags, EFlags::SaveBitmap | EFlags::SaveBitmapAsStencil);
	}

	const FPooledRenderTargetDesc& Desc = Captured.Desc;

	FRDGTextureRef VisualizeTexture2D = Captured.Texture;

	// The RDG version may be stale. The IPooledRenderTarget overrides it.
	if (Captured.PooledRenderTarget)
	{
		Captured.Texture = nullptr;
		VisualizeTexture2D = GraphBuilder.RegisterExternalTexture(Captured.PooledRenderTarget, Desc.DebugName);
	}

	RDG_EVENT_SCOPE(GraphBuilder, "VisualizeTexture");

	using EInputUVMapping = FVisualizeTexture::EInputUVMapping;
	const EInputUVMapping InputUVMapping = VisualizeTexture2D->Desc.IsTexture2D() ? GVisualizeTexture.Config.InputUVMapping : EInputUVMapping::Whole;

	using EInputValueMapping = FVisualizeTexture::EInputValueMapping;
	const EInputValueMapping InputValueMapping = Captured.InputValueMapping;

	{
		FScreenPassTexture CopyInput(VisualizeTexture2D);
		FScreenPassRenderTarget CopyOutput = Output;

		switch (InputUVMapping)
		{
		case EInputUVMapping::LeftTop:
			CopyOutput.ViewRect = View.UnconstrainedViewRect;
			break;

		case EInputUVMapping::PixelPerfectCenter:
		{
			FIntPoint SrcSize = CopyInput.ViewRect.Size();
			FIntPoint Center = View.UnconstrainedViewRect.Size() / 2;
			FIntPoint HalfMin = SrcSize / 2;
			FIntPoint HalfMax = SrcSize - HalfMin;

			CopyOutput.ViewRect = FIntRect(Center - HalfMin, Center + HalfMax);
		}
		break;

		case EInputUVMapping::PictureInPicture:
		{
			const FIntPoint CopyInputExtent  = CopyInput.Texture->Desc.Extent;
			const float CopyInputAspectRatio = float(CopyInputExtent.X) / float(CopyInputExtent.Y);

			int32 TargetedHeight = 0.3f * View.UnconstrainedViewRect.Height();
			int32 TargetedWidth = CopyInputAspectRatio * TargetedHeight;
			int32 OffsetFromBorder = 100;

			CopyOutput.ViewRect.Min.X = View.UnconstrainedViewRect.Min.X + OffsetFromBorder;
			CopyOutput.ViewRect.Max.X = CopyOutput.ViewRect.Min.X + TargetedWidth;
			CopyOutput.ViewRect.Max.Y = View.UnconstrainedViewRect.Max.Y - OffsetFromBorder;
			CopyOutput.ViewRect.Min.Y = CopyOutput.ViewRect.Max.Y - TargetedHeight;
		}
		break;
		}

		AddDrawTexturePass(GraphBuilder, View, CopyInput, CopyOutput);
	}

	Output.LoadAction = ERenderTargetLoadAction::ELoad;

	const FIntPoint BufferSizeXY = FSceneRenderTargets::Get(GraphBuilder.RHICmdList).GetBufferSizeXY();

	AddDrawCanvasPass(GraphBuilder, {}, View, Output,
		[VisualizeTexture2D, BufferSizeXY, Desc, &View, InputUVMapping, InputValueMapping](FCanvas& Canvas)
	{
		float X = 100 + View.UnconstrainedViewRect.Min.X;
		float Y = 160 + View.UnconstrainedViewRect.Min.Y;
		float YStep = 14;

		{
			const uint32 VersionCount = GVisualizeTexture.GetVersionCount(VisualizeTexture2D->Name);

			FString ExtendedName;
			if (VersionCount > 0)
			{
				const uint32 Version = FMath::Min(GVisualizeTexture.Requested.Version.Get(VersionCount), VersionCount - 1);

				// was reused this frame
				ExtendedName = FString::Printf(TEXT("%s@%d @0..%d"), VisualizeTexture2D->Name, Version, VersionCount - 1);
			}
			else
			{
				// was not reused this frame but can be referenced
				ExtendedName = FString::Printf(TEXT("%s"), VisualizeTexture2D->Name);
			}

			const FVisualizeTexture::FConfig& Config = GVisualizeTexture.Config;

			FString Channels = TEXT("RGB");
			switch (Config.SingleChannel)
			{
			case 0: Channels = TEXT("R"); break;
			case 1: Channels = TEXT("G"); break;
			case 2: Channels = TEXT("B"); break;
			case 3: Channels = TEXT("A"); break;
			}
			float Multiplier = (Config.SingleChannel == -1) ? Config.RGBMul : Config.SingleChannelMul;

			FString Line = FString::Printf(TEXT("VisualizeTexture: \"%s\" %s*%g UV%d"),
				*ExtendedName,
				*Channels,
				Multiplier,
				InputUVMapping);

			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}
		{
			FString Line = FString::Printf(TEXT("   TextureInfoString(): %s"), *(Desc.GenerateInfoString()));
			Canvas.DrawShadowedString(X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}
		{
			FString Line = FString::Printf(TEXT("  BufferSize:(%d,%d)"), BufferSizeXY.X, BufferSizeXY.Y);
			Canvas.DrawShadowedString(X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}

		const FSceneViewFamily& ViewFamily = *View.Family;

		for (int32 ViewId = 0; ViewId < ViewFamily.Views.Num(); ++ViewId)
		{
			const FViewInfo* ViewIt = static_cast<const FViewInfo*>(ViewFamily.Views[ViewId]);
			FString Line = FString::Printf(TEXT("   View #%d: (%d,%d)-(%d,%d)"), ViewId + 1,
				ViewIt->UnscaledViewRect.Min.X, ViewIt->UnscaledViewRect.Min.Y, ViewIt->UnscaledViewRect.Max.X, ViewIt->UnscaledViewRect.Max.Y);
			Canvas.DrawShadowedString(X + 10, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}

		X += 40;

		if (Desc.Flags & TexCreate_CPUReadback)
		{
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Content cannot be visualized on the GPU (TexCreate_CPUReadback)"), GetStatsFont(), FLinearColor(1, 1, 0));
		}
		else
		{
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Blinking Red: <0"), GetStatsFont(), FLinearColor(1, 0, 0));
			Canvas.DrawShadowedString(X, Y += YStep, TEXT("Blinking Blue: NAN or Inf"), GetStatsFont(), FLinearColor(0, 0, 1));

			if (InputValueMapping == EInputValueMapping::Shadow)
			{
				Canvas.DrawShadowedString(X, Y += YStep, TEXT("Color Key: Linear with white near and teal distant"), GetStatsFont(), FLinearColor(54.f / 255.f, 117.f / 255.f, 136.f / 255.f));
			}
			else if (InputValueMapping == EInputValueMapping::Depth)
			{
				Canvas.DrawShadowedString(X, Y += YStep, TEXT("Color Key: Nonlinear with white distant"), GetStatsFont(), FLinearColor(0.5, 0, 0));
			}
		}
	});
#endif
}