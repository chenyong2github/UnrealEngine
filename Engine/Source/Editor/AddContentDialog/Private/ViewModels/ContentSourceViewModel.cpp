// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/ContentSourceViewModel.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Internationalization/Culture.h"
#include "Modules/ModuleManager.h"


#define LOCTEXT_NAMESPACE "ContentSourceViewModel"

uint32 FContentSourceViewModel::ImageID = 0;


FContentSourceViewModel::FContentSourceViewModel(TSharedPtr<IContentSource> ContentSourceIn)
{
	ContentSource = ContentSourceIn;
	SetupBrushes();
	Category = FCategoryViewModel(ContentSource->GetCategory());
}

TSharedPtr<IContentSource> FContentSourceViewModel::GetContentSource()
{
	return ContentSource;
}

FText FContentSourceViewModel::GetName()
{
	const FString CurrentLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	if (CachedNameText.Language != CurrentLanguage)
	{
		CachedNameText.Language = CurrentLanguage;
		CachedNameText.Text = ChooseLocalizedText(ContentSource->GetLocalizedNames(), CurrentLanguage);
	}
	return CachedNameText.Text;
}

FText FContentSourceViewModel::GetDescription()
{
	const FString CurrentLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	if (CachedDescriptionText.Language != CurrentLanguage)
	{
		CachedDescriptionText.Language = CurrentLanguage;
		CachedDescriptionText.Text = ChooseLocalizedText(ContentSource->GetLocalizedDescriptions(), CurrentLanguage);
	}
	return CachedDescriptionText.Text;
}

FText FContentSourceViewModel::GetAssetTypes()
{
	const FString CurrentLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	if (CachedAssetTypeText.Language != CurrentLanguage)
	{
		CachedAssetTypeText.Language = CurrentLanguage;
		CachedAssetTypeText.Text = ChooseLocalizedText(ContentSource->GetLocalizedAssetTypes(), CurrentLanguage);
	}
	return CachedAssetTypeText.Text;
}

FString FContentSourceViewModel::GetClassTypes()
{
	return ContentSource->GetClassTypesUsed();
}

FCategoryViewModel FContentSourceViewModel::GetCategory()
{
	return Category;
}

TSharedPtr<FSlateBrush> FContentSourceViewModel::GetIconBrush()
{
	return IconBrush;
}

TArray<TSharedPtr<FSlateBrush>>* FContentSourceViewModel::GetScreenshotBrushes()
{
	return &ScreenshotBrushes;
}

void FContentSourceViewModel::SetupBrushes()
{
	if (ContentSource->GetIconData().IsValid())
	{
	    FString IconBrushName = GetName().ToString() + "_" + ContentSource->GetIconData()->GetName();
	    IconBrush = CreateBrushFromRawData(IconBrushName, *ContentSource->GetIconData()->GetData());
	}

	for (TSharedPtr<FImageData> ScreenshotData : ContentSource->GetScreenshotData())
	{
		if (ScreenshotData.IsValid() == true)
		{
		    FString ScreenshotBrushName = GetName().ToString() + "_" + ScreenshotData->GetName();
		    ScreenshotBrushes.Add(CreateBrushFromRawData(ScreenshotBrushName, *ScreenshotData->GetData()));
	    }
    }
}

TSharedPtr<FSlateDynamicImageBrush> FContentSourceViewModel::CreateBrushFromRawData(FString ResourceNamePrefix, const TArray< uint8 >& RawData) const
{
	TSharedPtr< FSlateDynamicImageBrush > Brush;

	uint32 BytesPerPixel = 4;
	int32 Width = 0;
	int32 Height = 0;

	bool bSucceeded = false;
	TArray<uint8> DecodedImage;
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (ImageWrapper.IsValid() && (RawData.Num() > 0) && ImageWrapper->SetCompressed(RawData.GetData(), RawData.Num()))
	{
		Width = ImageWrapper->GetWidth();
		Height = ImageWrapper->GetHeight();

		const TArray<uint8>* RawImageData = NULL;
		if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, RawImageData))
		{
			DecodedImage.AddUninitialized(Width * Height * BytesPerPixel);
			DecodedImage = *RawImageData;
			bSucceeded = true;
		}
	}

	if (bSucceeded)
	{
		FString UniqueResourceName = ResourceNamePrefix + "_" + FString::FromInt(ImageID++);
		Brush = FSlateDynamicImageBrush::CreateWithImageData(FName(*UniqueResourceName), FVector2D(ImageWrapper->GetWidth(), ImageWrapper->GetHeight()), DecodedImage);
	}

	return Brush;
}

FText FContentSourceViewModel::ChooseLocalizedText(const TArray<FLocalizedText>& Choices, const FString& InCurrentLanguage)
{
	auto FindLocalizedTextForCulture = [&Choices](const FString& InCulture)
	{
		return Choices.FindByPredicate([&InCulture](const FLocalizedText& LocalizedText)
		{
			return InCulture == LocalizedText.GetTwoLetterLanguage();
		});
	};

	// Try and find a prioritized localized translation
	{
		const TArray<FString> PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(InCurrentLanguage);
		for (const FString& CultureName : PrioritizedCultureNames)
		{
			if (const FLocalizedText* LocalizedTextForCulture = FindLocalizedTextForCulture(CultureName))
			{
				return LocalizedTextForCulture->GetText();
			}
		}
	}

	// We failed to find a localized translation, see if we have English text available to use
	if (InCurrentLanguage != TEXT("en"))
	{
		if (const FLocalizedText* LocalizedTextForEnglish = FindLocalizedTextForCulture(TEXT("en")))
		{
			return LocalizedTextForEnglish->GetText();
		}
	}

	// We failed to find English, see if we have any translations available to use
	if (Choices.Num() > 0)
	{
		return Choices[0].GetText();
	}

	return FText();
}

#undef LOCTEXT_NAMESPACE
