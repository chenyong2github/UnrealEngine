// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "INetworkFileSystemModule.h"
#include "NetworkFileSystemLog.h"
#include "NetworkFileServer.h"
#include "NetworkFileServerHttp.h"
#include "Interfaces/ITargetPlatformManagerModule.h"


DEFINE_LOG_CATEGORY(LogFileServer);


/**
 * Implements the NetworkFileSystem module.
 */
class FNetworkFileSystemModule
	: public INetworkFileSystemModule
{
public:

	// INetworkFileSystemModule interface

	virtual INetworkFileServer* CreateNetworkFileServer( bool bLoadTargetPlatforms, int32 Port, FNetworkFileDelegateContainer NetworkFileDelegateContainer, const ENetworkFileServerProtocol Protocol ) const
	{
		FNetworkFileServerOptions FileServerOptions;
		FileServerOptions.Protocol = Protocol;
		FileServerOptions.Port = Port;
		FileServerOptions.Delegates = MoveTemp(NetworkFileDelegateContainer);
		FileServerOptions.bRestrictPackageAssetsToSandbox = false; 
		
		return CreateNetworkFileServer(MoveTemp(FileServerOptions), bLoadTargetPlatforms);
	}
	
	virtual INetworkFileServer* CreateNetworkFileServer(FNetworkFileServerOptions FileServerOptions, bool bLoadTargetPlatforms) const override
	{
		if (bLoadTargetPlatforms)
		{
			ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();

			// if we didn't specify a target platform then use the entire target platform list (they could all be possible!)
			FString Platforms;
			if (FParse::Value(FCommandLine::Get(), TEXT("TARGETPLATFORM="), Platforms))
			{
				FileServerOptions.TargetPlatforms =  TPM.GetActiveTargetPlatforms();
			}
			else
			{
				FileServerOptions.TargetPlatforms = TPM.GetTargetPlatforms();
			}
		}

		switch (FileServerOptions.Protocol)
		{
#if ENABLE_HTTP_FOR_NFS
		case NFSP_Http: 
			return new FNetworkFileServerHttp(MoveTemp(FileServerOptions));
#endif
		case NFSP_Tcp:
			return new FNetworkFileServer(MoveTemp(FileServerOptions));
		}
 
		return nullptr;
	}
};


IMPLEMENT_MODULE(FNetworkFileSystemModule, NetworkFileSystem);
