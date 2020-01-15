// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyle.h"

#define HAIRSTRANDSEDITOR_MODULE_NAME TEXT("HairStrandsEditor")

class IHairStrandsTranslator;

/** Implements the HairStrands module  */
class HAIRSTRANDSEDITOR_API FHairStrandsEditor : public IModuleInterface
{
public:

	//~ IModuleInterface interface

	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsDynamicReloading() override
	{
		return false;
	}

	static inline FHairStrandsEditor& Get()
	{
		return FModuleManager::LoadModuleChecked<FHairStrandsEditor>(HAIRSTRANDSEDITOR_MODULE_NAME);
	}

	/** Register HairStrandsTranslator to add support for import by the HairStandsFactory */
	template <typename TranslatorType>
	void RegisterHairTranslator()
	{
		TranslatorSpawners.Add([]
		{
			return MakeShared<TranslatorType>();
		});
	}

	/** Get new instances of HairStrandsTranslators */
	TArray<TSharedPtr<IHairStrandsTranslator>> GetHairTranslators();

private:

	/** The collection of registered asset type actions. */
	TArray<TSharedRef<class IAssetTypeActions>> RegisteredAssetTypeActions;

	TArray<TFunction<TSharedPtr<IHairStrandsTranslator>()>> TranslatorSpawners;

	TSharedPtr<FSlateStyleSet> StyleSet;
};
