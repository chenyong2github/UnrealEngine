// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineUtils.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistryModule.h"
#include "MoviePipelineSetting.h"
#include "ARFilter.h"
#include "AssetData.h"
#include "UObject/UObjectIterator.h"
#include "HAL/IConsoleManager.h"
#include "MoviePipelineAntiAliasingSetting.h"

namespace UE
{
	namespace MovieRenderPipeline
	{
		TArray<UClass*> FindMoviePipelineSettingClasses()
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

			TArray<FAssetData> ClassList;

			FARFilter Filter;
			Filter.ClassNames.Add(UMoviePipelineSetting::StaticClass()->GetFName());

			// Include any Blueprint based objects as well, this includes things like Blutilities, UMG, and GameplayAbility objects
			Filter.bRecursiveClasses = true;
			AssetRegistryModule.Get().GetAssets(Filter, ClassList);

			TArray<UClass*> Classes;

			for (const FAssetData& Data : ClassList)
			{
				UClass* Class = Data.GetClass();
				if (Class)
				{
					Classes.Add(Class);
				}
			}

			for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
			{
				if (ClassIterator->IsChildOf(UMoviePipelineSetting::StaticClass()) && !ClassIterator->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
				{
					Classes.Add(*ClassIterator);
				}
			}

			return Classes;
		}
	
		/**
		* Returns the anti-aliasing setting that we should use. This defaults to the project setting, and then
		* uses the one specified by the setting if overriden.
		*/
		EAntiAliasingMethod GetEffectiveAntiAliasingMethod(const UMoviePipelineAntiAliasingSetting* InSetting)
		{
			EAntiAliasingMethod AntiAliasingMethod = EAntiAliasingMethod::AAM_None;

			IConsoleVariable* AntiAliasingCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.DefaultFeature.AntiAliasing"));
			if (AntiAliasingCVar)
			{
				int32 Value = AntiAliasingCVar->GetInt();
				if (Value >= 0 && Value < AAM_MAX)
				{
					AntiAliasingMethod = (EAntiAliasingMethod)Value;
				}
			}

			if (InSetting)
			{
				if (InSetting->bOverrideAntiAliasing)
				{
					AntiAliasingMethod = InSetting->AntiAliasingMethod;
				}
			}

			return AntiAliasingMethod;
		}
	}
}

namespace MoviePipeline
{
	void GetPassCompositeData(FMoviePipelineMergerOutputFrame* InMergedOutputFrame, TArray<MoviePipeline::FCompositePassInfo>& OutCompositedPasses)
	{
		for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
		{
			FImagePixelDataPayload* Payload = RenderPassData.Value->GetPayload<FImagePixelDataPayload>();
			if (Payload->bCompositeToFinalImage)
			{
				// Burn in data should always be 8 bit values, this is assumed later when we composite.
				check(RenderPassData.Value->GetType() == EImagePixelType::Color);

				FCompositePassInfo& Info = OutCompositedPasses.AddDefaulted_GetRef();
				Info.PassIdentifier = RenderPassData.Key;
				Info.PixelData = RenderPassData.Value->CopyImageData();
			}
		}
	}
}