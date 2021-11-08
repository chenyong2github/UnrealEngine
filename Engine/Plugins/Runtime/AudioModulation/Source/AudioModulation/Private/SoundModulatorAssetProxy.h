// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "IAudioModulation.h"


namespace AudioModulation
{
	template <typename TModObjectType>
	class TSoundModulatorAssetProxy : public FSoundModulatorAssetProxy
	{
	public:
		explicit TSoundModulatorAssetProxy(TModObjectType& InModulatorObject)
		{
			if (!GEngine)
			{
				return;
			}

			FAudioDeviceHandle AudioDevice;
			if (UWorld* World = GEngine->GetWorldFromContextObject(&InModulatorObject, EGetWorldErrorMode::ReturnNull))
			{
				if (!World->bAllowAudioPlayback || World->IsNetMode(NM_DedicatedServer))
				{
					return;
				}

				AudioDevice = World->GetAudioDevice();
			}
			else
			{
				AudioDevice = GEngine->GetMainAudioDevice();
			}

			if (!AudioDevice.IsValid() || !AudioDevice->IsModulationPluginEnabled())
			{
				return;
			}

			if (IAudioModulation* Modulation = AudioDevice->ModulationInterface.Get())
			{
				const FName ParameterName = InModulatorObject.GetOutputParameterName();
				ModHandle = Audio::FModulatorHandle(*Modulation, &InModulatorObject, ParameterName);
				Parameter = Modulation->GetParameter(ParameterName);
			}
		}

		virtual Audio::IProxyDataPtr Clone() const override
		{
			return TUniquePtr<TSoundModulatorAssetProxy<TModObjectType>>(new TSoundModulatorAssetProxy<TModObjectType>(*this));
		}

		virtual float GetValue() const override
		{
			float Value = 1.0f;
			if (ModHandle.IsValid())
			{
				ModHandle.GetValueThreadSafe(Value);
			}
			return Value;
		}

		virtual const Audio::FModulationParameter& GetParameter() const override
		{
			return Parameter;
		}

	private:
		Audio::FModulatorHandle ModHandle;
		Audio::FModulationParameter Parameter;
	};
} // namespace AudioModulation