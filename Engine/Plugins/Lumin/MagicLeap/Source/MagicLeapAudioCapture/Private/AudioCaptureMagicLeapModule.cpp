// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "AudioCaptureDeviceInterface.h"
#include "AudioCaptureMagicLeap.h"
#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"

namespace Audio
{
	class FAudioCaptureMagicLeapFactory : public IAudioCaptureFactory
	{
	public:
		virtual TUniquePtr<IAudioCaptureStream> CreateNewAudioCaptureStream() override
		{
			return TUniquePtr<IAudioCaptureStream>(new FAudioCaptureMagicLeapStream());
		}
	};

	class FAudioCaptureMagicLeapModule : public IModuleInterface
	{
	private:
		FAudioCaptureMagicLeapFactory AudioCaptureFactory;

	public:
		virtual void StartupModule() override
		{
			IModularFeatures::Get().RegisterModularFeature(IAudioCaptureFactory::GetModularFeatureName(), &AudioCaptureFactory);
		}

		virtual void ShutdownModule() override
		{
			IModularFeatures::Get().UnregisterModularFeature(IAudioCaptureFactory::GetModularFeatureName(), &AudioCaptureFactory);
		}
	};
}

IMPLEMENT_MODULE(Audio::FAudioCaptureMagicLeapModule, MagicLeapAudioCapture)
