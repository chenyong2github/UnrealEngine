// Copyright Epic Games, Inc. All Rights Reserved.

#include "RPCTestBase.h"
#if WITH_RPCLIB
#include "RPCWrapper/Server.h"
#include "RPCWrapper/rpclib_includes.h"

#define LOCTEXT_NAMESPACE "AITestSuite_UE4MLTest"

PRAGMA_DISABLE_OPTIMIZATION


void FRPCTestBase::TearDown() 
{
	U4MLManager::Get().GetOnAddClientFunctions().Remove(BindClientHandle);
	U4MLManager::Get().GetOnAddServerFunctions().Remove(BindServerHandle);
	U4MLManager::Get().StopServer();

	delete RPCClient;
	RPCClient = nullptr;

	FAITestBase::TearDown();
}


struct FRPCTest_StartStop : public FRPCTestBase
{
	virtual bool Update() override
	{
		U4MLManager::Get().StartServer(DefaultServerPort, EUE4MLServerMode::Client);
		Test(TEXT("Is server running"), U4MLManager::Get().IsRunning() == true);
		U4MLManager::Get().StopServer();
		Test(TEXT("Is server stopped"), U4MLManager::Get().IsRunning() == false);

		return true;
	}
};
IMPLEMENT_AI_LATENT_TEST(FRPCTest_StartStop, "System.Engine.AI.ML.RPC.ServerStartStop")

struct FRPCTest_BasicBinds : public FRPCTestBase
{
	uint8 bClientFooCalled : 1;
	uint8 bServerFooCalled : 1;
	uint8 CallCount : 6;
	EUE4MLServerMode ServerMode = EUE4MLServerMode::Client;
	  
	FRPCTest_BasicBinds() : bClientFooCalled(false), bServerFooCalled(false)
	{}

	virtual void SetUp() override
	{
		U4MLManager::Get().StartServer(DefaultServerPort, ServerMode);
		RPCClient = new rpc::client("127.0.0.1", DefaultServerPort);
	}

	// wait for any of the functions to get called checking CallCount
	// virtual bool Update() override

	virtual void SetUpClientBinds() override
	{
		U4MLManager::Get().AddClientFunctionBind(UE4_RPC_BIND("client_foo", [this]()
		{
			bClientFooCalled = true; 
			++CallCount;
		}));
	}
	virtual void SetUpServerBinds() override
	{
		U4MLManager::Get().AddServerFunctionBind(UE4_RPC_BIND("server_foo", [this]()
		{ 
			bServerFooCalled = true; 
			++CallCount;
		}));
	}
	virtual void TearDown() override
	{
		FRPCTestBase::TearDown();
	}
};

struct FRPCTest_ClientBinds : public FRPCTest_BasicBinds
{
	virtual void SetUp() override
	{
		FRPCTest_BasicBinds::SetUp();		
		// ordering this way to make sure we first call the function that's not 
		// likely to throw an exception. RPC client will throw one if function of 
		// given name is not found 
		try
		{
			RPCClient->call("client_foo");
			RPCClient->call("server_foo");
		}
		catch (...)
		{
			// this is expected if we call a function that has not been bound
		}
	}
	virtual bool Update() override
	{
		if (CallCount)
		{
			Test(TEXT("Only one function should get called"), CallCount == 1);
			Test(TEXT("Only the client function should get called"), bClientFooCalled);
			Test(TEXT("The server function should not get called"), bServerFooCalled == false);
			return true;
		}
		return false;
	}
};
IMPLEMENT_AI_LATENT_TEST(FRPCTest_ClientBinds, "System.Engine.AI.ML.RPC.ClientBinds")

struct FRPCTest_ServerBinds : public FRPCTest_BasicBinds
{
	virtual void SetUp() override
	{
		ServerMode = EUE4MLServerMode::Server;
		FRPCTest_BasicBinds::SetUp();
		// ordering this way to make sure we first call the function that's not 
		// likely to throw an exception. RPC client will throw one if function of 
		// given name is not found 
		try
		{
			RPCClient->call("server_foo");
			RPCClient->call("client_foo");
		}
		catch (...)
		{
			// this is expected if we call a function that has not been bound
		}
	}

	virtual bool Update() override
	{
		if (CallCount)
		{
			Test(TEXT("Only one function should get called"), CallCount == 1);
			Test(TEXT("Only the server function should get called"), bServerFooCalled);
			Test(TEXT("The client function should not get called"), bClientFooCalled == false);
			return true;
		}
		return false;
	}
};
IMPLEMENT_AI_LATENT_TEST(FRPCTest_ServerBinds, "System.Engine.AI.ML.RPC.ServerBinds")

PRAGMA_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE

#endif // WITH_RPCLIB
