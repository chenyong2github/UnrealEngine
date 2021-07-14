// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintThumbnailRenderer.h"

#include "Blueprint/UserWidget.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/HittestGrid.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprint.h"
#include "Widgets/SVirtualWindow.h"

#define LOCTEXT_NAMESPACE "UWidgetBlueprintThumbnailRenderer"

bool UWidgetBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Object);
	return (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UWidget::StaticClass()));
}

void UWidgetBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	// Create a plain gray background for the thumbnail
	const int32 SizeOfUV = 1;
	FLinearColor GrayBackgroundColor(FVector4(.03f, .03f, .03f, 1.f));
	Canvas->DrawTile(
		0.0f, 0.0f, Width, Height,
		0.0f, 0.0f, SizeOfUV, SizeOfUV,
		GrayBackgroundColor);


	// Convert the object from UObject to SWidget
	UWidgetBlueprint* WidgetBlueprintToRender = Cast<UWidgetBlueprint>(Object);

	const bool bIsBlueprintValid = IsValid(WidgetBlueprintToRender)
		&& IsValid(WidgetBlueprintToRender->GeneratedClass)
		&& WidgetBlueprintToRender->bHasBeenRegenerated
		//&& Blueprint->IsUpToDate() - This condition makes the thumbnail blank whenever the BP is dirty. It seems too strict.
		&& !WidgetBlueprintToRender->bBeingCompiled
		&& !WidgetBlueprintToRender->HasAnyFlags(RF_Transient);

	TSharedPtr<SWidget> WindowContent;
	UClass* ClassToGenerate = WidgetBlueprintToRender->GeneratedClass;
	if (bIsBlueprintValid && ClassToGenerate->IsChildOf(UWidget::StaticClass()))
	{
		if (UUserWidget* WidgetInstance = NewObject<UUserWidget>(GetTransientPackage(), ClassToGenerate))
		{
			WidgetInstance->Initialize();
			WidgetInstance->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);

			WindowContent = WidgetInstance->TakeWidget();
		}
	}

	if (WindowContent == nullptr)
	{
		return;
	}

	//Create Window
	FVector2D WidgetSize(Width, Height);
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(WidgetSize);
	TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
	Window->SetContent(WindowContent.ToSharedRef());
	Window->Resize(WidgetSize);

	// Store the desired size to maintain the aspect ratio later
	FGeometry WindowGeometry = FGeometry::MakeRoot(WidgetSize, FSlateLayoutTransform(1.0f));
	Window->SlatePrepass(1.0f);
	FVector2D DesiredSizeWindow = Window->GetDesiredSize();

	
	if (DesiredSizeWindow.X < SMALL_NUMBER || DesiredSizeWindow.Y < SMALL_NUMBER)
	{
		return;
	}

	// Create Renderer Target and WidgetRenderer
	bool bApplyGammaCorrection = false;
	FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(bApplyGammaCorrection);
	WidgetRenderer->SetIsPrepassNeeded(false);
	const bool bIsLinearSpace = !bApplyGammaCorrection;

	if (!RenderTarget2D)
	{
		RenderTarget2D  = NewObject<UTextureRenderTarget2D>();
		RenderTarget2D->Filter = TF_Bilinear;
		RenderTarget2D->ClearColor = FLinearColor::Transparent;
		RenderTarget2D->SRGB = true;
		RenderTarget2D->TargetGamma = 1;
	}

	const EPixelFormat RequestedFormat = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
	RenderTarget2D->InitCustomFormat(DesiredSizeWindow.X, DesiredSizeWindow.Y, RequestedFormat, true);

	WidgetRenderer->DrawWindow(RenderTarget2D, *HitTestGrid, Window, 1.0f, DesiredSizeWindow, 0.1f);

	// Draw the SWidget on canvas

	TTuple<FVector2D, FVector2D> ScaledSizeAndOffset = GetScaledSizeAndOffset(DesiredSizeWindow.X, DesiredSizeWindow.Y, Width, Height);
	FVector2D DrawOffset = ScaledSizeAndOffset.Get<1>();
	WidgetSize = ScaledSizeAndOffset.Get<0>();

	FCanvasTileItem CanvasTile(FVector2D(X + DrawOffset.X, Y + DrawOffset.Y), RenderTarget2D->GetResource(), WidgetSize, FLinearColor::White);
	CanvasTile.BlendMode = SE_BLEND_Translucent;
	FlushRenderingCommands();
	CanvasTile.Draw(Canvas);

	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer);
		WidgetRenderer = nullptr;
	}

}

TTuple<FVector2D, FVector2D> UWidgetBlueprintThumbnailRenderer::GetScaledSizeAndOffset(float ImgWidth, float ImgHeight, float ThumbnailWidth, float ThumbnailHeight) const
{
	// Scale the widget blueprint image to fit in the thumbnail

	checkf(ImgWidth > 0.f && ImgHeight > 0.f, TEXT("The size should have been previously checked to be > 0."));

	FVector2D ScaledSize(0.0f,0.0f);
	FVector2D Offset(0.0f, 0.0f);
	if (ImgWidth > ImgHeight)
	{
		float RatioAdjust = ImgHeight / ImgWidth;
		Offset.Y = ThumbnailHeight * (1 - RatioAdjust) / 2;
		ScaledSize.Y = ThumbnailHeight * RatioAdjust;
		ScaledSize.X = ThumbnailWidth;
	}
	else
	{
		float RatioAdjust = ImgWidth / ImgHeight;
		Offset.X = ThumbnailWidth * (1 - RatioAdjust) / 2;
		ScaledSize.X = ThumbnailWidth * RatioAdjust;
		ScaledSize.Y = ThumbnailHeight;
	}
	
	
	return TTuple<FVector2D, FVector2D>(ScaledSize, Offset);
}

#undef LOCTEXT_NAMESPACE