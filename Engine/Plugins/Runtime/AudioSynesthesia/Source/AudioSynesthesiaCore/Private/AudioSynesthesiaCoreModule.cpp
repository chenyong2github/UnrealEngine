// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioSynesthesiaCoreModule.h"
#include "AudioSynesthesiaCoreLog.h"
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "LoudnessNRTFactory.h"
#include "ConstantQNRTFactory.h"
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
				IModularFeatures::Get().RegisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessFactory);
				IModularFeatures::Get().RegisterModularFeature(FConstantQNRTFactory::GetModularFeatureName(), &ConstantQFactory);
				IModularFeatures::Get().RegisterModularFeature(FOnsetNRTFactory::GetModularFeatureName(), &OnsetFactory);
			}

			void ShutdownModule()
			{
				// Unregister factories on shutdown
				IModularFeatures::Get().UnregisterModularFeature(FLoudnessNRTFactory::GetModularFeatureName(), &LoudnessFactory);
				IModularFeatures::Get().UnregisterModularFeature(FConstantQNRTFactory::GetModularFeatureName(), &ConstantQFactory);
				IModularFeatures::Get().UnregisterModularFeature(FOnsetNRTFactory::GetModularFeatureName(), &OnsetFactory);
			}

		private:
			FLoudnessNRTFactory LoudnessFactory;
			FConstantQNRTFactory ConstantQFactory;
			FOnsetNRTFactory OnsetFactory;
	};

}

IMPLEMENT_MODULE(Audio::FAudioSynesthesiaCoreModule, AudioSynesthesiaCore);

