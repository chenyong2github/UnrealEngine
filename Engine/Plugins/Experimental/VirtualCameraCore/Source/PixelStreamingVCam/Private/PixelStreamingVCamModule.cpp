// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingVCamModule.h"

#include "BuiltinProviders/VCamPixelStreamingSession.h"
#include "IDecoupledOutputProviderModule.h"
#include "VCamPixelStreamingSessionLogic.h"

#include "Modules/ModuleManager.h"

namespace UE::PixelStreamingVCam::Private
{
	void FPixelStreamingVCamModule::StartupModule()
	{
		using namespace DecoupledOutputProvider;
		IDecoupledOutputProviderModule& DecouplingModule = IDecoupledOutputProviderModule::Get();
		DecouplingModule.RegisterLogicFactory(
			UVCamPixelStreamingSession::StaticClass(),
			FOutputProviderLogicFactoryDelegate::CreateLambda([](const FOutputProviderLogicCreationArgs& Args)
			{
				return MakeShared<FVCamPixelStreamingSessionLogic>();
			})
		);
	}

	void FPixelStreamingVCamModule::ShutdownModule()
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("DecoupledOutputProvider")))
		{
			DecoupledOutputProvider::IDecoupledOutputProviderModule::Get().UnregisterLogicFactory(UVCamPixelStreamingSession::StaticClass());
		}
	}
}

IMPLEMENT_MODULE(UE::PixelStreamingVCam::Private::FPixelStreamingVCamModule, PixelStreamingVCam);
