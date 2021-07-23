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
#include "WidgetBlueprintEditorUtils.h"

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
	UUserWidget* WidgetInstance = nullptr;
	if (bIsBlueprintValid && ClassToGenerate->IsChildOf(UWidget::StaticClass()))
	{
		WidgetInstance = NewObject<UUserWidget>(GetTransientPackage(), ClassToGenerate);
		if (WidgetInstance)
		{
			WidgetInstance->Initialize();
			WidgetInstance->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);


			WindowContent = WidgetInstance->TakeWidget();
		}
	}

	if (WindowContent == nullptr || WidgetInstance == nullptr)
	{
		return;
	}


	//Create Window
	FVector2D ThumbnailSize(Width, Height);
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(ThumbnailSize);
	TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
	Window->SetContent(WindowContent.ToSharedRef());
	Window->Resize(ThumbnailSize);

	// Store the desired size to maintain the aspect ratio later
	FGeometry WindowGeometry = FGeometry::MakeRoot(ThumbnailSize, FSlateLayoutTransform(1.0f));
	Window->SlatePrepass(1.0f);
	FVector2D DesiredSizeWindow = Window->GetDesiredSize();


	if (DesiredSizeWindow.X < SMALL_NUMBER || DesiredSizeWindow.Y < SMALL_NUMBER)
	{
		return;
	}

	FVector2D UnscaledSize = FWidgetBlueprintEditorUtils::GetWidgetPreviewUnScaledCustomSize(DesiredSizeWindow, WidgetInstance);
	if (UnscaledSize.X < SMALL_NUMBER || UnscaledSize.Y < SMALL_NUMBER)
	{
		return;
	}
	TTuple<float, FVector2D> ScaleAndOffset = GetScaleAndOffset(UnscaledSize, ThumbnailSize);
	float Scale = ScaleAndOffset.Get<0>();
	FVector2D Offset = ScaleAndOffset.Get<1>();
	FVector2D ScaledSize = UnscaledSize * Scale;

	// Create Renderer Target and WidgetRenderer
	bool bApplyGammaCorrection = false;
	FWidgetRenderer* WidgetRenderer = new FWidgetRenderer(bApplyGammaCorrection);
	const bool bIsLinearSpace = !bApplyGammaCorrection;

	if (!RenderTarget2D)
	{
		RenderTarget2D = NewObject<UTextureRenderTarget2D>();
		RenderTarget2D->Filter = TF_Bilinear;
		RenderTarget2D->ClearColor = FLinearColor::Transparent;
		RenderTarget2D->SRGB = true;
		RenderTarget2D->TargetGamma = 1;
	}

	const EPixelFormat RequestedFormat = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
	RenderTarget2D->InitCustomFormat(ScaledSize.X, ScaledSize.Y, RequestedFormat, true);
	WidgetRenderer->DrawWindow(RenderTarget2D, *HitTestGrid, Window, Scale, ScaledSize, 0.1f);

	// Draw the SWidget on canvas
	FCanvasTileItem CanvasTile(FVector2D(X + Offset.X, Y + Offset.Y), RenderTarget2D->GetResource(), ScaledSize, FLinearColor::White);
	CanvasTile.BlendMode = SE_BLEND_Translucent;
	FlushRenderingCommands();
	CanvasTile.Draw(Canvas);

	if (WidgetRenderer)
	{
		BeginCleanup(WidgetRenderer);
		WidgetRenderer = nullptr;
	}
	

}

TTuple<float, FVector2D> UWidgetBlueprintThumbnailRenderer::GetScaleAndOffset(FVector2D WidgetSize, FVector2D ThumbnailSize) const
{
	// Scale the widget blueprint image to fit in the thumbnail

	checkf(WidgetSize.X > 0.f && WidgetSize.Y > 0.f, TEXT("The size should have been previously checked to be > 0."));

	float Scale;
	float XOffset = 0;
	float YOffset = 0;
	if (WidgetSize.X > WidgetSize.Y)
	{
		Scale = ThumbnailSize.X / WidgetSize.X;
		WidgetSize *= Scale;
		YOffset = (ThumbnailSize.Y - WidgetSize.Y) / 2.f;
	}
	else
	{
		Scale = ThumbnailSize.Y / WidgetSize.Y;
		WidgetSize *= Scale;
		XOffset = (ThumbnailSize.X - WidgetSize.X) / 2.f;
	}

	return TTuple<float, FVector2D>(Scale, FVector2D(XOffset, YOffset));
}


#undef LOCTEXT_NAMESPACE