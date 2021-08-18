// Copyright Epic Games, Inc. All Rights Reserved.

#include "WidgetBlueprintThumbnailRenderer.h"

#include "Blueprint/UserWidget.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/HittestGrid.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprint.h"
#include "Widgets/SVirtualWindow.h"
#include "WidgetBlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "UWidgetBlueprintThumbnailRenderer"

class FWidgetBlueprintThumbnailPool
{
public:
	struct FInstance
	{
		TWeakObjectPtr<UClass> WidgetClass;
		TWeakObjectPtr<UTextureRenderTarget2D> RenderTarget2D;
		TWeakObjectPtr<UUserWidget> Widget;
	};

public:
	static constexpr int32 MaxNumInstance = 50;

	FWidgetBlueprintThumbnailPool()
	{
		InstancedThumbnails.Reserve(MaxNumInstance);
	}

	~FWidgetBlueprintThumbnailPool()
	{
		Clear();
	}

	FInstance* FindThumbnail(const UClass* InClass) const
	{
		check(InClass);
		const FName ClassName = InClass->GetFName();

		return InstancedThumbnails.FindRef(ClassName);
	}

	FInstance& EnsureThumbnail(const UClass* InClass)
	{
		check(InClass);
		const FName ClassName = InClass->GetFName();

		FInstance* ExistingThumbnail = InstancedThumbnails.FindRef(ClassName);
		if (!ExistingThumbnail)
		{
			if (InstancedThumbnails.Num() >= MaxNumInstance)
			{
				Clear();
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			}

			ExistingThumbnail = new FInstance;
			InstancedThumbnails.Add(ClassName, ExistingThumbnail);
		}

		return *ExistingThumbnail;
	}

	void RemoveThumbnail(const UClass* InClass)
	{
		check(InClass);
		InstancedThumbnails.Remove(InClass->GetFName());
	}

	void Clear()
	{
		for(auto& Instance : InstancedThumbnails)
		{
			delete Instance.Value;
		}
		InstancedThumbnails.Reset();
	}

private:
	TMap<FName, FInstance*> InstancedThumbnails;
};


void UWidgetBlueprintThumbnailRenderer::FWidgetBlueprintThumbnailPoolDeleter::operator()(FWidgetBlueprintThumbnailPool* Pointer)
{
	delete Pointer;
}


UWidgetBlueprintThumbnailRenderer::UWidgetBlueprintThumbnailRenderer()
	: ThumbnailPool(new FWidgetBlueprintThumbnailPool)
{
	FKismetEditorUtilities::OnBlueprintUnloaded.AddUObject(this, &UWidgetBlueprintThumbnailRenderer::OnBlueprintUnloaded);
}


UWidgetBlueprintThumbnailRenderer::~UWidgetBlueprintThumbnailRenderer()
{
	FKismetEditorUtilities::OnBlueprintUnloaded.RemoveAll(this);
}


bool UWidgetBlueprintThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(Object);
	return (Blueprint && Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UWidget::StaticClass()));
}


void UWidgetBlueprintThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
#if !UE_SERVER

	if (Width < 1 || Height < 1)
	{
		return;
	}

	if (!FApp::CanEverRender())
	{
		return;
	}

	UWidgetBlueprint* WidgetBlueprintToRender = Cast<UWidgetBlueprint>(Object);
	const bool bIsBlueprintValid = IsValid(WidgetBlueprintToRender)
		&& IsValid(WidgetBlueprintToRender->GeneratedClass)
		&& WidgetBlueprintToRender->bHasBeenRegenerated
		&& !WidgetBlueprintToRender->bBeingCompiled
		&& !WidgetBlueprintToRender->HasAnyFlags(RF_Transient)
		&& !WidgetBlueprintToRender->GeneratedClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_Abstract)
		&& WidgetBlueprintToRender->GeneratedClass->IsChildOf(UWidget::StaticClass());
	if (!bIsBlueprintValid)
	{
		return;
	}

	// Create a plain gray background for the thumbnail
	const int32 SizeOfUV = 1;
	FLinearColor GrayBackgroundColor(FVector4(.03f, .03f, .03f, 1.f));
	Canvas->DrawTile(
		0.0f, 0.0f, Width, Height,
		0.0f, 0.0f, SizeOfUV, SizeOfUV,
		GrayBackgroundColor);

	// check if an image is used instead of auto generating the thumbnail
	if (WidgetBlueprintToRender->ThumbnailImage)
	{
		FVector2D TextureSize(WidgetBlueprintToRender->ThumbnailImage->GetSizeX(), WidgetBlueprintToRender->ThumbnailImage->GetSizeY());
		if (TextureSize.X > SMALL_NUMBER && TextureSize.Y > SMALL_NUMBER)
		{
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
		UUserWidget* WidgetInstance = nullptr;
		UTextureRenderTarget2D* RenderTarget2D = nullptr;

		{
			UClass* ClassToGenerate = WidgetBlueprintToRender->GeneratedClass;
			FWidgetBlueprintThumbnailPool::FInstance& ThumbnailInstance = ThumbnailPool->EnsureThumbnail(ClassToGenerate);

			WidgetInstance = ThumbnailInstance.Widget.Get();
			if (!WidgetInstance)
			{
				ThumbnailInstance.Widget = NewObject<UUserWidget>(GetTransientPackage(), ClassToGenerate);
				ThumbnailInstance.Widget->Initialize();
				ThumbnailInstance.Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);

				WidgetInstance = ThumbnailInstance.Widget.Get();
			}

			RenderTarget2D = ThumbnailInstance.RenderTarget2D.Get();
			if (!RenderTarget2D)
			{
				ThumbnailInstance.RenderTarget2D = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
				ThumbnailInstance.RenderTarget2D->Filter = TF_Bilinear;
				ThumbnailInstance.RenderTarget2D->ClearColor = FLinearColor::Transparent;
				ThumbnailInstance.RenderTarget2D->SRGB = true;
				ThumbnailInstance.RenderTarget2D->TargetGamma = 1;

				RenderTarget2D = ThumbnailInstance.RenderTarget2D.Get();
			}
		}

		FVector2D ThumbnailSize(Width, Height);
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
#endif
}


void UWidgetBlueprintThumbnailRenderer::OnBlueprintUnloaded(UBlueprint* Blueprint)
{
	if (Blueprint && Blueprint->GeneratedClass)
	{
		ThumbnailPool->RemoveThumbnail(Blueprint->GeneratedClass);
	}
}

#undef LOCTEXT_NAMESPACE