// Copyright Epic Games, Inc. All Rights Reserved.
#include "AudioSynesthesiaCoreModule.h"

#include "AudioSynesthesiaCoreLog.h"
#include "ConstantQNRTFactory.h"
#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"
#include "LoudnessFactory.h"
#include "LoudnessNRTFactory.h"
#include "MeterFactory.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "OnsetNRTFactory.h"

DEFINE_LOG_CATEGORY(LogAudioSynesthesiaCore);

namespace Audio
{
	class FAudioSynesthesiaCoreModule : public IAudioSynesthesiaCoreModule
	{
		public:
			void StartupModule()
			{
				// Register factories on startup
				IModularFeatures::Get().RegisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessNRTFactory);
				IModularFeatures::Get().RegisterModularFeature(FConstantQNRTFactory::GetModularFeatureName(), &ConstantQNRTFactory);
				IModularFeatures::Get().RegisterModularFeature(FOnsetNRTFactory::GetModularFeatureName(), &OnsetNRTFactory);

				IModularFeatures::Get().RegisterModularFeature(FLoudnessFactory::GetModularFeatureName(), &LoudnessFactory);
				IModularFeatures::Get().RegisterModularFeature(FMeterFactory::GetModularFeatureName(), &MeterFactory);
			}

			void ShutdownModule()
			{
				// Unregister factories on shutdown
				IModularFeatures::Get().UnregisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessNRTFactory);
				IModularFeatures::Get().UnregisterModularFeature(FConstantQNRTFactory::GetModularFeatureName(), &ConstantQNRTFactory);
				IModularFeatures::Get().UnregisterModularFeature(FOnsetNRTFactory::GetModularFeatureName(), &OnsetNRTFactory);

				IModularFeatures::Get().UnregisterModularFeature(FLoudnessFactory::GetModularFeatureName(), &LoudnessFactory);
				IModularFeatures::Get().UnregisterModularFeature(FMeterFactory::GetModularFeatureName(), &MeterFactory);
			}

		private:
			FLoudnessNRTFactory LoudnessNRTFactory;
			FConstantQNRTFactory ConstantQNRTFactory;
			FOnsetNRTFactory OnsetNRTFactory;

			FLoudnessFactory LoudnessFactory;
			FMeterFactory MeterFactory;
	};

}

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaCoreModule, AudioSynesthesiaCore);

