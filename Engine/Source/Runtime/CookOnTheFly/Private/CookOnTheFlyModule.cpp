// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFly.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogCookOnTheFly);

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyConnectionServer> MakeCookOnTheFlyConnectionServer(FCookOnTheFlyServerOptions Options);
	
TUniquePtr<ICookOnTheFlyServerConnection> MakeServerConnection(const UE::Cook::FCookOnTheFlyHostOptions& HostOptions);

}}

class FCookOnTheFlyModule final
	: public UE::Cook::ICookOnTheFlyModule
{
public:

	//
	// IModuleInterface interface
	//

	virtual void StartupModule( ) override { }

	virtual void ShutdownModule( ) override { }
	
	virtual bool SupportsDynamicReloading( ) override { return false; }

	//
	// ICookOnTheFlyModule interface
	//

	virtual TUniquePtr<UE::Cook::ICookOnTheFlyConnectionServer> CreateConnectionServer(UE::Cook::FCookOnTheFlyServerOptions Options) override
	{
		return UE::Cook::MakeCookOnTheFlyConnectionServer(Options);
	}

	virtual TUniquePtr<UE::Cook::ICookOnTheFlyServerConnection> ConnectToServer(const UE::Cook::FCookOnTheFlyHostOptions& HostOptions) override
	{
		return UE::Cook::MakeServerConnection(HostOptions);
	}
};

IMPLEMENT_MODULE(FCookOnTheFlyModule, CookOnTheFly);
