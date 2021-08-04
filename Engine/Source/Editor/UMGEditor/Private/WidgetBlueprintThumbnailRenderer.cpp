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

	// check if an image is used instead of auto generating the thumbnail
	if (WidgetBlueprintToRender->ThumbnailImage)
	{
		FVector2D TextureSize(WidgetBlueprintToRender->ThumbnailImage->GetSizeX(), WidgetBlueprintToRender->ThumbnailImage->GetSizeY());
		if (TextureSize.X > SMALL_NUMBER && TextureSize.Y > SMALL_NUMBER) {
			TTuple<float, FVector2D> ScaleAndOffset = FWidgetBlueprintEditorUtils::GetThumbnailImageScaleAndOffset(TextureSize, FVector2D(Width, Height));
			float Scale = ScaleAndOffset.Get<0>();
			FVector2D Offset = ScaleAndOffset.Get<1>();
			FVector2D ThumbnailImageOffset = Offset;
			FVector2D ThumbnailImageScaledSize = Scale * TextureSize;

			FCanvasTileItem CanvasTile(ThumbnailImageOffset, WidgetBlueprintToRender->ThumbnailImage->GetResource(), ThumbnailImageScaledSize, FLinearColor::White);
			CanvasTile.BlendMode = SE_BLEND_Translucent;
			CanvasTile.Draw(Canvas);
		}
	}
	else
	{
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
			}
		}

		if (WidgetInstance == nullptr)
		{
			return;
		}
		FVector2D ThumbnailSize(Width, Height);

		if (!RenderTarget2D)
		{
			RenderTarget2D = NewObject<UTextureRenderTarget2D>();
			RenderTarget2D->Filter = TF_Bilinear;
			RenderTarget2D->ClearColor = FLinearColor::Transparent;
			RenderTarget2D->SRGB = true;
			RenderTarget2D->TargetGamma = 1;
		}

		TOptional<FWidgetBlueprintEditorUtils::FWidgetThumbnailProperties> ScaleAndOffset;

		if (WidgetBlueprintToRender->ThumbnailSizeMode == EThumbnailPreviewSizeMode::Custom)
		{
			ScaleAndOffset = FWidgetBlueprintEditorUtils::DrawSWidgetInRenderTargetForThumbnail(WidgetInstance, RenderTarget2D, ThumbnailSize, WidgetBlueprintToRender->ThumbnailCustomSize, WidgetBlueprintToRender->ThumbnailSizeMode);
		}
		else
		{
			ScaleAndOffset = FWidgetBlueprintEditorUtils::DrawSWidgetInRenderTargetForThumbnail(WidgetInstance, RenderTarget2D, ThumbnailSize, TOptional<FVector2D>(), WidgetBlueprintToRender->ThumbnailSizeMode);

		}
		if (!ScaleAndOffset.IsSet())
		{
			return;
		}
		FVector2D Offset = ScaleAndOffset.GetValue().Offset;
		FVector2D ScaledSize = ScaleAndOffset.GetValue().ScaledSize;

		// Draw the SWidget on canvas
		FCanvasTileItem CanvasTile(FVector2D(X + Offset.X, Y + Offset.Y), RenderTarget2D->GetResource(), ScaledSize, FLinearColor::White);
		CanvasTile.BlendMode = SE_BLEND_Translucent;
		FlushRenderingCommands();
		CanvasTile.Draw(Canvas);
	}

}


#undef LOCTEXT_NAMESPACE