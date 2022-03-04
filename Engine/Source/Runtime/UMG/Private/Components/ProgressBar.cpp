// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ProgressBar.h"
#include "Slate/SlateBrushAsset.h"
#include "Styling/UMGCoreStyle.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UProgressBar


UE_FIELD_NOTIFICATION_IMPLEMENT_CLASS_DESCRIPTOR_ThreeFields(UProgressBar, Percent, FillColorAndOpacity, bIsMarquee);


static FProgressBarStyle* DefaultProgressBarStyle = nullptr;

#if WITH_EDITOR
static FProgressBarStyle* EditorProgressBarStyle = nullptr;
#endif 

UProgressBar::UProgressBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (DefaultProgressBarStyle == nullptr)
	{
		DefaultProgressBarStyle = new FProgressBarStyle(FUMGCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar"));

		// Unlink UMG default colors.
		DefaultProgressBarStyle->UnlinkColors();
	}

	WidgetStyle = *DefaultProgressBarStyle;

#if WITH_EDITOR 
	if (EditorProgressBarStyle == nullptr)
	{
		EditorProgressBarStyle = new FProgressBarStyle(FCoreStyle::Get().GetWidgetStyle<FProgressBarStyle>("ProgressBar"));

		// Unlink UMG Editor colors from the editor settings colors.
		EditorProgressBarStyle->UnlinkColors();
	}

	if (IsEditorWidget())
	{
		WidgetStyle = *EditorProgressBarStyle;

		// The CDO isn't an editor widget and thus won't use the editor style, call post edit change to mark difference from CDO
		PostEditChange();
	}
#endif // WITH_EDITOR

	WidgetStyle.FillImage.TintColor = FLinearColor::White;

	BarFillType = EProgressBarFillType::LeftToRight;
	BarFillStyle = EProgressBarFillStyle::Mask;
	bIsMarquee = false;
	Percent = 0;
	FillColorAndOpacity = FLinearColor::White;
	BorderPadding = FVector2D(0, 0);
}

void UProgressBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyProgressBar.Reset();
}

TSharedRef<SWidget> UProgressBar::RebuildWidget()
{
	MyProgressBar = SNew(SProgressBar);

	return MyProgressBar.ToSharedRef();
}

void UProgressBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	TAttribute< TOptional<float> > PercentBinding = OPTIONAL_BINDING_CONVERT(float, Percent, TOptional<float>, ConvertFloatToOptionalFloat);
	TAttribute<FSlateColor> FillColorAndOpacityBinding = PROPERTY_BINDING(FSlateColor, FillColorAndOpacity);

	MyProgressBar->SetStyle(&WidgetStyle);

	MyProgressBar->SetBarFillType(BarFillType);
	MyProgressBar->SetBarFillStyle(BarFillStyle);
	MyProgressBar->SetPercent(bIsMarquee ? TOptional<float>() : PercentBinding);
	MyProgressBar->SetFillColorAndOpacity(FillColorAndOpacityBinding);
	MyProgressBar->SetBorderPadding(BorderPadding);
}

void UProgressBar::SetIsMarquee(bool InbIsMarquee)
{
	if (bIsMarquee != InbIsMarquee)
	{
		bIsMarquee = InbIsMarquee;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::bIsMarquee);
	}

	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetPercent(bIsMarquee ? TOptional<float>() : Percent);
	}
}

void UProgressBar::SetFillColorAndOpacity(FLinearColor Color)
{
	if (FillColorAndOpacity != Color)
	{
		FillColorAndOpacity = Color;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::FillColorAndOpacity);
	}

	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetFillColorAndOpacity(FillColorAndOpacity);
	}
}

void UProgressBar::SetPercent(float InPercent)
{
	if (Percent != InPercent)
	{
		Percent = InPercent;
		BroadcastFieldValueChanged(FFieldNotificationClassDescriptor::Percent);
	}

	if (MyProgressBar.IsValid())
	{
		MyProgressBar->SetPercent(InPercent);
	}
}

#if WITH_EDITOR

const FText UProgressBar::GetPaletteCategory()
{
	return LOCTEXT("Common", "Common");
}

void UProgressBar::OnCreationFromPalette()
{
	FillColorAndOpacity = FLinearColor(0, 0.5f, 1.0f);
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
