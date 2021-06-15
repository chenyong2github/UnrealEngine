// Copyright Epic Games, Inc. All Rights Reserved.

#include "CookOnTheFly.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogCookOnTheFly);

namespace UE { namespace Cook
{

TUniquePtr<ICookOnTheFlyConnectionServer> MakeCookOnTheFlyConnectionServer(FCookOnTheFlyServerOptions Options);
	
TUniquePtr<ICookOnTheFlyServerConnection> MakeServerConnection();

}}

class FCookOnTheFlyModule final
	: public UE::Cook::ICookOnTheFlyModule
{
public:

	//
	// IModuleInterface interface
	//

	virtual void StartupModule( ) override { }

	virtual void ShutdownModule( ) override
	{ 
		if (ServerConnection.IsValid())
		{
			ServerConnection->Disconnect();
			ServerConnection.Reset();
		}
	}

	virtual bool SupportsDynamicReloading( ) override { return false; }

	//
	// ICookOnTheFlyModule interface
	//

	virtual TUniquePtr<UE::Cook::ICookOnTheFlyConnectionServer> CreateConnectionServer(UE::Cook::FCookOnTheFlyServerOptions Options) override
	{
		return UE::Cook::MakeCookOnTheFlyConnectionServer(Options);
	}

	virtual bool ConnectToServer(const UE::Cook::FCookOnTheFlyHostOptions& HostOptions) override
	{
		if (!ServerConnection.IsValid())
		{
			ServerConnection = UE::Cook::MakeServerConnection();
			return ServerConnection->Connect(HostOptions);
		}

		return ServerConnection->IsConnected();
	}

	virtual UE::Cook::ICookOnTheFlyServerConnection& GetServerConnection() override
	{
		checkf(ServerConnection.IsValid(), TEXT("Call connect before accessing the server connection"));
		return *ServerConnection;
	}

private:
	TUniquePtr<UE::Cook::ICookOnTheFlyServerConnection> ServerConnection;
};

IMPLEMENT_MODULE(FCookOnTheFlyModule, CookOnTheFly);
