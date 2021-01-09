// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAudioAnalyzerNRTInterface.h"

namespace Audio
{

	IAnalyzerNRTFactory* GetAnalyzerNRTFactory(FName InFactoryName);

	/** FAnalyzerNRTFacade
	 *
	 * FAnalyzerNRTFacade provides a simplified interface for running 
	 * analyzer factories over complete audio resources.
	 */
	class AUDIOANALYZER_API FAnalyzerNRTFacade
	{
		public:
			/**
			 * Create an FAnalyzerNRTFacade with the analyzer settings and factory name.
			 */
			FAnalyzerNRTFacade(TUniquePtr<IAnalyzerNRTSettings> InSettings, const FName& InFactoryName);

			/**
			 * Analyze an entire PCM16 encoded audio object.  Audio for the entire sound should be contained within InRawWaveData.
			 */
			TUniquePtr<IAnalyzerNRTResult> AnalyzePCM16Audio(const TArray<uint8>& InRawWaveData, int32 InNumChannels, float InSampleRate);

		private:

			TUniquePtr<IAnalyzerNRTSettings> Settings;
			FName FactoryName;
	};
}
