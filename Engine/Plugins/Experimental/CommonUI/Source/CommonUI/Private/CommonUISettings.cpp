// Copyright Epic Games, Inc. All Rights Reserved.

#include "CommonUISettings.h"

#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

UCommonUISettings::UCommonUISettings(const FObjectInitializer& Initializer)
	: Super(Initializer)
	, bAutoLoadData(true)
	, bDefaultDataLoaded(false)
{}

void UCommonUISettings::LoadData()
{
	LoadEditorData();
}

#if WITH_EDITOR
void UCommonUISettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	bDefaultDataLoaded = false;
	AutoLoadData();
}
#endif

void UCommonUISettings::LoadEditorData()
{
	if (!bDefaultDataLoaded)
	{
		DefaultImageResourceObjectInstance = DefaultImageResourceObject.LoadSynchronous();
		DefaultThrobberMaterialInstance = DefaultThrobberMaterial.LoadSynchronous();

		TSubclassOf<UCommonUIRichTextData> RichTextDataClass = DefaultRichTextDataClass.LoadSynchronous();
		RichTextDataInstance = RichTextDataClass.GetDefaultObject();

		if (GUObjectArray.IsDisregardForGC(this))
		{
			if (DefaultImageResourceObjectInstance)
			{
				DefaultImageResourceObjectInstance->AddToRoot();
			}
			if (DefaultThrobberMaterialInstance)
			{
				DefaultThrobberMaterialInstance->AddToRoot();
			}
			if (RichTextDataInstance)
			{
				RichTextDataInstance->AddToRoot();
			}
		}

		DefaultThrobberBrush.SetResourceObject(DefaultThrobberMaterialInstance);
		DefaultThrobberBrush.ImageSize = FVector2D(64.f, 64.f);
		DefaultThrobberBrush.ImageType = ESlateBrushImageType::FullColor;
		DefaultThrobberBrush.DrawAs = DefaultThrobberMaterialInstance ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;

		bDefaultDataLoaded = true;
	}
}

void UCommonUISettings::AutoLoadData()
{
	if (bAutoLoadData)
	{
		LoadData();
	}
}

UCommonUIRichTextData* UCommonUISettings::GetRichTextData() const
{
	ensure(bDefaultDataLoaded);

	return RichTextDataInstance;
}

const FSlateBrush& UCommonUISettings::GetDefaultThrobberBrush() const
{
	ensure(bDefaultDataLoaded);

	return DefaultThrobberBrush;
}

UObject* UCommonUISettings::GetDefaultImageResourceObject() const
{
	ensure(bDefaultDataLoaded);

	return DefaultImageResourceObjectInstance;
}