// Copyright Epic Games, Inc. All Rights Reserved.

#include "Components/Throbber.h"
#include "SlateFwd.h"
#include "Slate/SlateBrushAsset.h"
#include "Styling/UMGCoreStyle.h"

#define LOCTEXT_NAMESPACE "UMG"

/////////////////////////////////////////////////////
// UThrobber

static FSlateBrush* DefaultThrobberBrush = nullptr;

#if WITH_EDITOR
static FSlateBrush* EditorThrobberBrush = nullptr;
#endif 

UThrobber::UThrobber(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NumberOfPieces = 3;

	bAnimateVertically = true;
	bAnimateHorizontally = true;
	bAnimateOpacity = true;

	if (DefaultThrobberBrush == nullptr)
	{
		DefaultThrobberBrush = new FSlateBrush(*FUMGCoreStyle::Get().GetBrush("Throbber.Chunk"));

		// Unlink UMG default colors.
		DefaultThrobberBrush->UnlinkColors();
	}

	Image = *DefaultThrobberBrush;

#if WITH_EDITOR 
	if (EditorThrobberBrush == nullptr)
	{
		EditorThrobberBrush = new FSlateBrush(*FCoreStyle::Get().GetBrush("Throbber.Chunk"));

		// Unlink UMG Editor colors from the editor settings colors.
		EditorThrobberBrush->UnlinkColors();
	}
	
	if (IsEditorWidget())
	{
		Image = *EditorThrobberBrush;

		// The CDO isn't an editor widget and thus won't use the editor style, call post edit change to mark difference from CDO
		PostEditChange();
	}
#endif // WITH_EDITOR
}

void UThrobber::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	MyThrobber.Reset();
}

TSharedRef<SWidget> UThrobber::RebuildWidget()
{
	MyThrobber = SNew(SThrobber)
		.PieceImage(&Image)
		.NumPieces(FMath::Clamp(NumberOfPieces, 1, 25))
		.Animate(GetAnimation());

	return MyThrobber.ToSharedRef();
}

void UThrobber::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	MyThrobber->SetNumPieces(FMath::Clamp(NumberOfPieces, 1, 25));
	MyThrobber->SetAnimate(GetAnimation());
}

SThrobber::EAnimation UThrobber::GetAnimation() const
{
	const int32 AnimationParams = (bAnimateVertically ? SThrobber::Vertical : 0) |
		(bAnimateHorizontally ? SThrobber::Horizontal : 0) |
		(bAnimateOpacity ? SThrobber::Opacity : 0);

	return static_cast<SThrobber::EAnimation>(AnimationParams);
}

void UThrobber::SetNumberOfPieces(int32 InNumberOfPieces)
{
	NumberOfPieces = InNumberOfPieces;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetNumPieces(FMath::Clamp(NumberOfPieces, 1, 25));
	}
}

void UThrobber::SetAnimateHorizontally(bool bInAnimateHorizontally)
{
	bAnimateHorizontally = bInAnimateHorizontally;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber::SetAnimateVertically(bool bInAnimateVertically)
{
	bAnimateVertically = bInAnimateVertically;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber:: SetAnimateOpacity(bool bInAnimateOpacity)
{
	bAnimateOpacity = bInAnimateOpacity;
	if (MyThrobber.IsValid())
	{
		MyThrobber->SetAnimate(GetAnimation());
	}
}

void UThrobber::PostLoad()
{
	Super::PostLoad();

	if ( GetLinkerUEVersion() < VER_UE4_DEPRECATE_UMG_STYLE_ASSETS )
	{
		if ( PieceImage_DEPRECATED != nullptr )
		{
			Image = PieceImage_DEPRECATED->Brush;
			PieceImage_DEPRECATED = nullptr;
		}
	}
}

#if WITH_EDITOR

const FText UThrobber::GetPaletteCategory()
{
	return LOCTEXT("Primitive", "Primitive");
}

#endif

/////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
