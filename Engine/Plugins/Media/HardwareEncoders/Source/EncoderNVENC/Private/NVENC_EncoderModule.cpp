// Copyright Epic Games, Inc. All Rights Reserved.

#include "Modules/ModuleManager.h"

#include "NVENC_Common.h"
#include "NVENC_EncoderH264.h"
#include "VideoEncoderFactory.h"

#include "Misc/CoreDelegates.h"

class FNVENCEncoderModule : public IModuleInterface
{
public:
	void StartupModule()
	{
		using namespace AVEncoder;

		FNVENCCommon& NVENC = FNVENCCommon::Setup();

		if (NVENC.GetIsAvailable())
		{
#if PLATFORM_LINUX && WITH_CUDA
			FModuleManager::LoadModuleChecked<FCUDAModule>("CUDA").OnPostCUDAInit.AddLambda([]() {FVideoEncoderNVENC_H264::Register(FVideoEncoderFactory::Get());});
#else
			FCoreDelegates::OnPostEngineInit.AddLambda([]() {FVideoEncoderNVENC_H264::Register(FVideoEncoderFactory::Get());});
#endif
		}
	}
};

IMPLEMENT_MODULE(FNVENCEncoderModule, EncoderNVENC);
