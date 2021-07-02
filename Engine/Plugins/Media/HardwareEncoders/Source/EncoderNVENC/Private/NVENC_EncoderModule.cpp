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
			FCoreDelegates::OnPostEngineInit.AddLambda([]() {FVideoEncoderNVENC_H264::Register(FVideoEncoderFactory::Get());});
		}
	}
};

IMPLEMENT_MODULE(FNVENCEncoderModule, EncoderNVENC);
