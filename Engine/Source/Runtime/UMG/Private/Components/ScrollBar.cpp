// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/ScrollBar.h"
#include "UObject/EditorObjectVersion.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UScrollBar

static FScrollBarStyle* DefaultScrollBarStyle = nullptr;

UScrollBar::UScrollBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsVariable = false;

	bAlwaysShowScrollbar = true;
	bAlwaysShowScrollbarTrack = true;
	Orientation = Orient_Vertical;
	Thickness = FVector2D(16.0f, 16.0f);
	Padding = FMargin(2.0f);

	if (DefaultScrollBarStyle == nullptr)
	{
		// HACK: THIS SHOULD NOT COME FROM CORESTYLE AND SHOULD INSTEAD BE DEFINED BY ENGINE TEXTURES/PROJECT SETTINGS
		DefaultScrollBarStyle = new FScrollBarStyle(FCoreStyle::Get().GetWidgetStyle<FScrollBarStyle>("Scrollbar"));

		// Unlink UMG default colors from the editor settings colors.
		DefaultScrollBarStyle->UnlinkColors();
	}
	
	WidgetStyle = *DefaultScrollBarStyle;
}

void UScrollBar::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyScrollBar.Reset();
}

TSharedRef<SWidget> UScrollBar::RebuildWidget()
{
	MyScrollBar = SNew(SScrollBar)
		.Style(&WidgetStyle)
		.AlwaysShowScrollbar(bAlwaysShowScrollbar)
		.AlwaysShowScrollbarTrack(bAlwaysShowScrollbarTrack)
		.Orientation(Orientation)
		.Thickness(Thickness)
		.Padding(Padding);

	//SLATE_EVENT(FOnUserScrolled, OnUserScrolled)

	return MyScrollBar.ToSharedRef();
}

void UScrollBar::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	//MyScrollBar->SetScrollOffset(DesiredScrollOffset);
}

void UScrollBar::SetState(float InOffsetFraction, float InThumbSizeFraction)
{
	if ( MyScrollBar.IsValid() )
	{
		MyScrollBar->SetState(InOffsetFraction, InThumbSizeFraction);
	}
}

#if WITH_EDITORONLY_DATA

void UScrollBar::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	const bool bDeprecateThickness = Ar.IsLoading() && Ar.CustomVer(FEditorObjectVersion::GUID) < FEditorObjectVersion::ScrollBarThicknessChange;
	if (bDeprecateThickness)
	{
		// Set Thickness property to previous default value.
		Thickness.Set(12.0f, 12.0f);
	}

	Super::Serialize(Ar);

	if (bDeprecateThickness)
	{
		// Implicit padding of 2 was removed, so Thickness value must be incremented by 4.
		Thickness += FVector2D(4.0f, 4.0f);
	}
}

void UScrollBar::PostLoad()
{
	Super::PostLoad();

	if ( GetLinkerUE4Version() < VER_UE4_DEPRECATE_UMG_STYLE_ASSETS )
	{
		if ( Style_DEPRECATED != nullptr )
		{
			const FScrollBarStyle* StylePtr = Style_DEPRECATED->GetStyle<FScrollBarStyle>();
			if ( StylePtr != nullptr )
			{
				WidgetStyle = *StylePtr;
			}

			Style_DEPRECATED = nullptr;
		}
	}
}

#endif // if WITH_EDITORONLY_DATA

#if WITH_EDITOR

const FText UScrollBar::GetPaletteCategory()
{
	return LOCTEXT("Primitive", "Primitive");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
