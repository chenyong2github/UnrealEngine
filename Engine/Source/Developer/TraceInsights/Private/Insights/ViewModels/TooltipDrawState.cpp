// Copyright Epic Games, Inc. All Rights Reserved.

#include "Insights/ViewModels/TooltipDrawState.h"

#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/CoreStyle.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/InsightsStyle.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

FLinearColor FTooltipDrawState::DefaultTitleColor(0.9f, 0.9f, 0.5f, 1.0f);
FLinearColor FTooltipDrawState::DefaultNameColor(0.6f, 0.6f, 0.6f, 1.0f);
FLinearColor FTooltipDrawState::DefaultValueColor(1.0f, 1.0f, 1.0f, 1.0f);

////////////////////////////////////////////////////////////////////////////////////////////////////

FTooltipDrawState::FTooltipDrawState()
	: WhiteBrush(FInsightsStyle::Get().GetBrush("WhiteBrush"))
	, Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
	, BackgroundColor(0.05f, 0.05f, 0.05f, 1.0f)
	, Size(0.0f, 0.0f)
	, DesiredSize(0.0f, 0.0f)
	, Position(0.0f, 0.0f)
	, ValueOffsetX(0.0f)
	, NewLineY(0.0f)
	, Opacity(0.0f)
	, DesiredOpacity(0.0f)
	, Texts()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FTooltipDrawState::~FTooltipDrawState()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::Reset()
{
	Opacity = 0.0f;
	DesiredOpacity = 0.0f;

	ResetContent();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::ResetContent()
{
	Texts.Reset();

	ValueOffsetX = 0.0f;
	NewLineY = BorderY;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::AddTitle(const FString& Title)
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D TextSize = FontMeasureService->Measure(Title, Font);
	Texts.Add({ BorderX, NewLineY, TextSize, Title, DefaultTitleColor, FDrawTextType::Title });

	NewLineY += DefaultTitleHeight;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::AddTitle(const FString& Title, const FLinearColor& Color)
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D TextSize = FontMeasureService->Measure(Title, Font);
	Texts.Add({ BorderX, NewLineY, TextSize, Title, Color, FDrawTextType::Misc });

	NewLineY += DefaultTitleHeight;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::AddNameValueTextLine(const FString& Name, const FString& Value)
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D NameTextSize = FontMeasureService->Measure(Name, Font);
	Texts.Add({ 0.0f, NewLineY, NameTextSize, Name, DefaultNameColor, FDrawTextType::Name });

	const FVector2D ValueTextSize = FontMeasureService->Measure(Value, Font);
	Texts.Add({ 0.0f, NewLineY, ValueTextSize, Value, DefaultValueColor, FDrawTextType::Value });

	NewLineY += DefaultLineHeight;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::AddTextLine(const FString& Text, const FLinearColor& Color)
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D TextSize = FontMeasureService->Measure(Text, Font);
	Texts.Add({ BorderX, NewLineY, TextSize, Text, Color, FDrawTextType::Misc });

	NewLineY += DefaultLineHeight;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::AddTextLine(const float X, const float Y, const FString& Text, const FLinearColor& Color)
{
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const FVector2D TextSize = FontMeasureService->Measure(Text, Font);
	Texts.Add({ X, Y, TextSize, Text, Color, FDrawTextType::Misc });
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::UpdateLayout()
{
	ValueOffsetX = 0.0f;
	for (const FDrawTextInfo& TextInfo : Texts)
	{
		if (TextInfo.Type == FDrawTextType::Name)
		{
			if (ValueOffsetX < TextInfo.TextSize.X)
			{
				ValueOffsetX = TextInfo.TextSize.X;
			}
		}
	}
	ValueOffsetX += BorderX;

	DesiredSize.Set(BorderX, NewLineY + BorderY);
	for (const FDrawTextInfo& TextInfo : Texts)
	{
		float RightX;
		switch (TextInfo.Type)
		{
		case FDrawTextType::Name:
			RightX = ValueOffsetX;
			break;

		case FDrawTextType::Value:
			RightX = ValueOffsetX + NameValueDX + TextInfo.TextSize.X;
			break;

		default:
			RightX = TextInfo.X + TextInfo.TextSize.X;
		}
		if (DesiredSize.X < RightX)
		{
			DesiredSize.X = RightX;
		}
	}
	DesiredSize.X += BorderX;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::Update()
{
	if (Size.X != DesiredSize.X)
	{
		Size.X = Size.X * 0.75f + DesiredSize.X * 0.25f;

		if (FMath::IsNearlyEqual(Size.X, DesiredSize.X))
		{
			Size.X = DesiredSize.X;
		}
	}

	if (Size.Y != DesiredSize.Y)
	{
		Size.Y = Size.Y * 0.5f + DesiredSize.Y * 0.5f;

		if (FMath::IsNearlyEqual(Size.Y, DesiredSize.Y))
		{
			Size.Y = DesiredSize.Y;
		}
	}

	float RealDesiredOpacity;
	if (DesiredSize.X > 1.0f)
	{
		const float DesiredOpacityByTooltipWidth = 1.0f - FMath::Abs(Size.X - DesiredSize.X) / DesiredSize.X;

		if (FMath::IsNearlyEqual(DesiredOpacity, DesiredOpacityByTooltipWidth, 0.001f))
		{
			RealDesiredOpacity = DesiredOpacity;
		}
		else
		{
			RealDesiredOpacity = FMath::Min(DesiredOpacity, DesiredOpacityByTooltipWidth);
		}
	}
	else
	{
		RealDesiredOpacity = 0.0f;
	}

	if (Opacity != RealDesiredOpacity)
	{
		if (Opacity < RealDesiredOpacity)
		{
			// slow fade in
			Opacity = Opacity * 0.9f + RealDesiredOpacity * 0.1f;
		}
		else
		{
			// fast fade out
			Opacity = Opacity * 0.75f + RealDesiredOpacity * 0.25f;
		}

		if (FMath::IsNearlyEqual(Opacity, RealDesiredOpacity, 0.001f))
		{
			Opacity = RealDesiredOpacity;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::SetPosition(const FVector2D& MousePosition, const float MinX, const float MaxX, const float MinY, const float MaxY)
{
	Position.X = FMath::Max(MinX, FMath::Min(MousePosition.X + 12.0f, MaxX - Size.X));
	Position.Y = FMath::Max(MinY, FMath::Min(MousePosition.Y + 15.0f, MaxY - Size.Y));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FTooltipDrawState::Draw(const FDrawContext& DrawContext) const
{
	if (Opacity > 0.0f && Size.X > 0.0f && Size.Y > 0.0f)
	{
		// Draw background.
		DrawContext.DrawBox(Position.X, Position.Y, Size.X, Size.Y, WhiteBrush, BackgroundColor.CopyWithNewOpacity(Opacity));
		if (Size.X < DesiredSize.X)
		{
			DrawContext.DrawBox(Position.X + Size.X, Position.Y, DesiredSize.X - Size.X, Size.Y, WhiteBrush, BackgroundColor.CopyWithNewOpacity(Opacity * 0.5f));
		}
		DrawContext.LayerId++;

		// Draw border.
		//DrawContext.DrawBox(Position.X, Position.Y, Size.X, Size.Y, BorderBrush, BorderColor.CopyWithNewOpacity(Opacity));
		//DrawContext.LayerId++;

		// Draw cached texts.
		for (const FDrawTextInfo& TextInfo : Texts)
		{
			float X = Position.X;
			switch (TextInfo.Type)
			{
				case FDrawTextType::Name:
					X += ValueOffsetX - TextInfo.TextSize.X;
					break;

				case FDrawTextType::Value:
					X += ValueOffsetX + NameValueDX;
					break;

				default:
					X += TextInfo.X;
			}
			DrawContext.DrawText(X, Position.Y + TextInfo.Y, TextInfo.Text, Font, TextInfo.Color.CopyWithNewOpacity(Opacity));
		}
		DrawContext.LayerId++;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////
