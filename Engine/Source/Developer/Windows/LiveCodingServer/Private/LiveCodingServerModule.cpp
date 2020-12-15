// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveCodingServerModule.h"
#include "LiveCodingServer.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "External/LC_Logging.h"

IMPLEMENT_MODULE(FLiveCodingServerModule, LiveCodingServer)

DEFINE_LOG_CATEGORY_STATIC(LogLiveCodingServer, Display, All);

static void ServerOutputHandler(Logging::Channel::Enum Channel, Logging::Type::Enum Type, const wchar_t* const Text)
{
	FString TrimText = FString(Text).TrimEnd();
	switch (Type)
	{
	case Logging::Type::LOG_ERROR:
		UE_LOG(LogLiveCodingServer, Error, TEXT("%s"), *TrimText);
		break;
	case Logging::Type::LOG_WARNING:
		// There are some warnings generated in the dev channel that aren't really actionable by the users.
		// For example, warnings about symbols being eliminated by the linker.  It would be nice to just 
		// filter that specific warning, but we can't.
		if (Channel == Logging::Channel::DEV)
		{
			UE_LOG(LogLiveCodingServer, Verbose, TEXT("%s"), *TrimText);
		}
		else
		{
			UE_LOG(LogLiveCodingServer, Warning, TEXT("%s"), *TrimText);
		}
		break;
	default:
		UE_LOG(LogLiveCodingServer, Display, TEXT("%s"), *TrimText);
		break;
	}

	if (Channel == Logging::Channel::USER)
	{
		ELiveCodingLogVerbosity Verbosity;
		switch (Type)
		{
		case Logging::Type::LOG_SUCCESS:
			Verbosity = ELiveCodingLogVerbosity::Success;
			break;
		case Logging::Type::LOG_ERROR:
			Verbosity = ELiveCodingLogVerbosity::Failure;
			break;
		case Logging::Type::LOG_WARNING:
			Verbosity = ELiveCodingLogVerbosity::Warning;
			break;
		default:
			Verbosity = ELiveCodingLogVerbosity::Info;
			break;
		}
		GLiveCodingServer->GetLogOutputDelegate().ExecuteIfBound(Verbosity, Text);
	}
}

void FLiveCodingServerModule::StartupModule()
{
	Logging::SetOutputHandler(&ServerOutputHandler);

	GLiveCodingServer = new FLiveCodingServer();

	IModularFeatures::Get().RegisterModularFeature(LIVE_CODING_SERVER_FEATURE_NAME, GLiveCodingServer);
}

void FLiveCodingServerModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(LIVE_CODING_SERVER_FEATURE_NAME, GLiveCodingServer);

	if(GLiveCodingServer != nullptr)
	{
		delete GLiveCodingServer;
		GLiveCodingServer = nullptr;
	}

	Logging::SetOutputHandler(nullptr);
}
