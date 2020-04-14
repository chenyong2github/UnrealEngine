// Copyright Epic Games, Inc. All Rights Reserved.

#include "4MLManager.h"
#include "4MLTypes.h"
#include "4MLAsync.h"
#include "4MLSession.h"
#include "4MLScribe.h"
#include "4MLSpace.h"
#include "Agents/4MLAgent.h"
#include "Actuators/4MLActuator.h"
#include "Engine/GameEngine.h"

#include "RPCWrapper/Server.h"

#define REPORT_NOT_IMPLEMENTED() rpc::this_handler().respond_error("Not implemented yet")
#define CHECK_AGENTID(AgentID) { \
	if (HasSession() == false) \
	{ \
		rpc::this_handler().respond_error("No active session"); \
	}\
	if (GetSession().GetAgent(AgentID) == nullptr) \
	{ \
		rpc::this_handler().respond_error(std::make_tuple("No Agent of ID", AgentID)); \
	} \
} \


void U4MLManager::AddCommonFunctions()
{
	if (bCommonFunctionsAdded)
	{
		return;
	}

#if WITH_RPCLIB

	AddClientFunctionBind(UE4_RPC_BIND("list_functions", &F4MLScribe::ListFunctions)
		, TEXT("(), Lists all functions available through RPC"));

	AddClientFunctionBind(UE4_RPC_BIND("get_description", [](std::string const& ElementName) {
		return F4MLScribe::GetDescription(ElementName);
	})
		, TEXT("(string ElementName), Describes given element"));

	AddClientFunctionBind(UE4_RPC_BIND("list_sensor_types", &F4MLScribe::ListSensorTypes)
		, TEXT("(), Lists all sensor types available to agents. Note that some of sensors might not make sense in a given environment (like reading keyboard in an mouse-only game)."));

	AddClientFunctionBind(UE4_RPC_BIND("list_actuator_types", &F4MLScribe::ListActuatorTypes)
		, TEXT("(), Lists all actuator types available to agents. Note that some of actuators might not make sense in a given environment (like faking keyboard actions in an mouse-only game)."));

	AddClientFunctionBind(UE4_RPC_BIND("ping", []() {
		return true;
	})
		, TEXT("(), Checks if the RPC server is still alive and responding."));

	AddClientFunctionBind(UE4_RPC_BIND("get_name", []() {
		return std::string(TCHAR_TO_UTF8(GInternalProjectName));
	})
		, TEXT("(), Fetches a readable identifier of the environment the external client is connected to.")); 

	AddClientFunctionBind(TEXT("is_finished"), [this](FRPCServer& Serv) { Serv.bind("is_finished", [this](F4ML::FAgentID AgentID) 
	{
		return HasSession() == false || GetSession().IsDone() || GetSession().GetAgent(AgentID) == nullptr 
			|| GetSession().GetAgent(AgentID)->IsDone();
	});}
		, TEXT("(), Checks if the game/simulation/episode is done."));

	AddClientFunctionBind(UE4_RPC_BIND("batch_is_finished", [this](std::vector<F4ML::FAgentID> AgentIDs) {
		std::vector<bool> Results;
		if (HasSession() == false || GetSession().IsDone())
		{
			for (int Index = 0; Index < AgentIDs.size(); ++Index)
			{ 
				Results.push_back(true);
			}
		}
		else
		{
			U4MLSession& Session = GetSession();
			for (int Index = 0; Index < AgentIDs.size(); ++Index)
			{
				U4MLAgent* Agent = Session.GetAgent(AgentIDs[Index]);
				Results.push_back(Agent == nullptr || Agent->IsDone());
			}
		}
		return Results;
	})
		, TEXT("(), Multi-agent version of is_finished"));

	AddClientFunctionBind(UE4_RPC_BIND("exit", []() {
		FPlatformMisc::RequestExit(/*bForce=*/false);
	})
		, TEXT("(), Closes the UE4 instance."));

	AddClientFunctionBind(UE4_RPC_BIND("close_session", [this]() {
		U4MLManager::Get().SetSession(nullptr);
	})
		, TEXT("(), Checks if the game/simulation/episode is done."));

#endif // WITH_RPCLIB

	bCommonFunctionsAdded = true;
}

void U4MLManager::ConfigureAsClient()
{
	UE_LOG(LogUE4ML, Log, TEXT("\tconfiguring as client"));

	AddCommonFunctions();

#if WITH_RPCLIB
	AddClientFunctionBind(UE4_RPC_BIND("add_agent", [this]() {
		return CallOnGameThread<F4ML::FAgentID>([this]()
		{
			return GetSession().AddAgent();
		});
	})
		, TEXT("Adds a default agent for current environment. Returns added agent's ID if successful, uint(-1) if failed."));

	AddClientFunctionBind(UE4_RPC_BIND("get_agent_config", [this](F4ML::FAgentID AgentID) {
		CHECK_AGENTID(AgentID);
		const F4MLAgentConfig& Config = GetSession().GetAgent(AgentID)->GetConfig();
		return FSTRING_TO_STD(F4ML::StructToJsonString(Config));

	})
		, TEXT("(uint AgentID), Retrieved given agent's config in JSON formatted string"));

	AddClientFunctionBind(UE4_RPC_BIND("actions", [this](std::vector<float> ValueStream) {
		FString All;
		for (auto A : ValueStream)
		{
			All += FString::Printf(TEXT("%.2f, "), A);
		}
		UE_LOG(LogUE4ML, Log, TEXT("%s"), *All);
	})
		, TEXT(""));

	AddClientFunctionBind(UE4_RPC_BIND("act", [this](F4ML::FAgentID AgentID, std::vector<float> ValueStream) {
		CHECK_AGENTID(AgentID);
		U4MLAgent* Agent = GetSession().GetAgent(AgentID);
		check(Agent);
		//Agent->Act(std::vector<float> ValueStream);

		const uint8* DataPtr = (const uint8*)ValueStream.data();
		TArray<uint8> Buffer;
		Buffer.Append(DataPtr, ValueStream.size() * sizeof(float));
		F4MLMemoryReader Reader(Buffer);
		Agent->DigestActions(Reader);
	})
		, TEXT("Distributes the given values array amongst all the actuators, based on actions_space."));

	AddClientFunctionBind(UE4_RPC_BIND("batch_act", [this](std::vector<F4ML::FAgentID> AgentIDs, std::vector<std::vector<float>> ValueStreams) {
		if (HasSession() == false)
		{
			rpc::this_handler().respond_error("No active session");
		}

		for (int Index = 0; Index < AgentIDs.size(); ++Index)
		{
			U4MLAgent* Agent = GetSession().GetAgent(AgentIDs[Index]);
			if (Agent)
			{
				const uint8* DataPtr = (const uint8*)ValueStreams[Index].data();
				TArray<uint8> Buffer;
				Buffer.Append(DataPtr, ValueStreams[Index].size() * sizeof(float));
				F4MLMemoryReader Reader(Buffer);
				Agent->DigestActions(Reader);
			}
		}
	})
		, TEXT("A multi-agent version of \'act\' function"));

	AddClientFunctionBind(UE4_RPC_BIND("get_observations", [this](F4ML::FAgentID AgentID) {
		std::vector<float> Values;
		if (HasSession() && GetSession().GetAgent(AgentID))
		{
			TArray<uint8> Buffer;
			F4MLMemoryWriter Writer(Buffer);
			//Observations.TimeStamp = GetSession().GetTimestamp();
			GetSession().GetAgent(AgentID)->GetObservations(Writer);

			const float* DataPtr = (float*)Buffer.GetData();
			Values.assign(DataPtr, DataPtr + Buffer.Num() / sizeof(float));
		}
		return Values;
	}));

	//AddClientFunctionBind(UE4_RPC_BIND("batch_get_observations", [this](std::vector<F4ML::FAgentID> AgentIDs) {
	AddClientFunctionBind(TEXT("batch_get_observations"), [this](FRPCServer& Serv) 
	{ 
		Serv.bind("batch_get_observations", [this](std::vector<F4ML::FAgentID> AgentIDs) 
		{
			std::vector<std::vector<float>> Values;
			if (HasSession())
			{
				Values.resize(AgentIDs.size());
				for (int Index = 0; Index < AgentIDs.size(); ++Index)
				{
					U4MLAgent* Agent = GetSession().GetAgent(AgentIDs[Index]);
					if (Agent)
					{
						TArray<uint8> Buffer;
						F4MLMemoryWriter Writer(Buffer);
						//Observations.TimeStamp = GetSession().GetTimestamp();
						Agent->GetObservations(Writer);

						const float* DataPtr = (float*)Buffer.GetData();
						Values[Index].assign(DataPtr, DataPtr + Buffer.Num() / sizeof(float));
					}
				}
			}
			return Values;
		});
	});

	AddClientFunctionBind(UE4_RPC_BIND("get", [this]() {
		std::vector<std::vector<uint8>> Out;

		Out.push_back({ 0,1,2,3,4 });

		return Out;
	})
		, TEXT("."));

	AddClientFunctionBind(UE4_RPC_BIND("get_recent_agent", [this]() {
		return HasSession() ? F4ML::FAgentID(GetSession().GetAgentsCount() - 1) : F4ML::InvalidAgentID;
	})
		, TEXT("(), Fetch ID of the last created agent."));

	AddClientFunctionBind(UE4_RPC_BIND("get_reward", [this](F4ML::FAgentID AgentID) {
		CHECK_AGENTID(AgentID);
		return GetSession().GetAgent(AgentID)->GetReward();
	})
		, TEXT("(), Fetch current reward for given Agent."));

	AddClientFunctionBind(UE4_RPC_BIND("batch_get_rewards", [this](std::vector<F4ML::FAgentID> AgentIDs) {
		if (HasSession() == false)
		{
			rpc::this_handler().respond_error("No active session");
		}
		std::vector<float> Rewards;
		for (int Index = 0; Index < AgentIDs.size(); ++Index)
		{
			U4MLAgent* Agent = GetSession().GetAgent(AgentIDs[Index]);
			Rewards.push_back(Agent ? Agent->GetReward() : 0.f);
		}

		return Rewards;
	})
		, TEXT("(), Fetch current reward for given Agent."));

	
	AddClientFunctionBind(UE4_RPC_BIND("desc_action_space", [this](F4ML::FAgentID AgentID) {
		return CallOnGameThread<std::string>([this, AgentID]()
		{
			CHECK_AGENTID(AgentID);
			F4MLSpaceDescription SpaceDesc;
			GetSession().GetAgent(AgentID)->GetActionSpaceDescription(SpaceDesc);
			return FSTRING_TO_STD(SpaceDesc.ToJson());
		});
	}));

	// we're sending this call to game thread since if it's called right after 
	// "configure_agent" then this call will fetch pre-config state due to agent 
	// configuration being performed on the game thread
	AddClientFunctionBind(UE4_RPC_BIND("desc_observation_space", [this](F4ML::FAgentID AgentID) {
		return CallOnGameThread<std::string>([this, AgentID]()
		{
			CHECK_AGENTID(AgentID);
			F4MLSpaceDescription SpaceDesc;
			GetSession().GetAgent(AgentID)->GetObservationSpaceDescription(SpaceDesc);
			return FSTRING_TO_STD(SpaceDesc.ToJson());
		});
	})); 
	
	// would be nicer
	AddClientFunctionBind(UE4_RPC_BIND("reset", []() {
		CallOnGameThread<void>([]()
		{
			U4MLManager::Get().ResetWorld();
		});
	})
		, TEXT("(), Lets the 4ML manager know that the environments should be reset. The details of how this call is handles heavily depends on the environment itself."));

	AddClientFunctionBind(UE4_RPC_BIND("disconnect", [this](F4ML::FAgentID AgentID) {
		CHECK_AGENTID(AgentID);
		GetSession().RemoveAgent(AgentID);
	})
		, TEXT("(), Lets the 4ML session know that given agent will not continue and is to be removed from the session."));

	//----------------------------------------------------------------------//
	// For review  
	//----------------------------------------------------------------------//

	// this also means we're done messing up with the agent (configuring and all) 
	// and we're ready to roll
	AddClientFunctionBind(UE4_RPC_BIND("configure_agent", [this](F4ML::FAgentID AgentID, std::string const& JsonConfigString) {

		if (HasSession() == false)
		{
			rpc::this_handler().respond_error("No active session");
			return;
		}
		else if (GetSession().GetAgent(AgentID) == nullptr)
		{
			rpc::this_handler().respond_error(std::make_tuple("No Agent of ID", AgentID));
			return;
		}

		F4MLAgentConfig Config;
		F4ML::JsonStringToStruct(FString(JsonConfigString.c_str()), Config);

		CallOnGameThread<void>([this, Config, AgentID]()
		{
			GetSession().GetAgent(AgentID)->Configure(Config);
		});
	}));

	// combines 'add' and 'configure' agent
	AddClientFunctionBind(UE4_RPC_BIND("create_agent", [this](std::string const& JsonConfigString) {
		F4MLAgentConfig Config;
		F4ML::JsonStringToStruct(FString(JsonConfigString.c_str()), Config);

		return CallOnGameThread<F4ML::FAgentID>([this, Config]()
		{
			return GetSession().AddAgent(Config);
		});
	}));

	AddClientFunctionBind(UE4_RPC_BIND("is_agent_ready", [this](F4ML::FAgentID AgentID) {
		return HasSession() && GetSession().IsAgentReady(AgentID);
	}));

	AddClientFunctionBind(UE4_RPC_BIND("is_ready", [this]() {
		return CallOnGameThread<bool>([this]()
		{
			return HasSession() && GetSession().IsReady();
		});
	}));
	
	AddClientFunctionBind(UE4_RPC_BIND("get_sensor_description", [](std::string const& ClassName) {
		UClass* ResultClass = FindObject<UClass>(ANY_PACKAGE, ANSI_TO_TCHAR(ClassName.c_str()));
		if (ResultClass)
		{
			U4MLAgentElement* CDO = ResultClass->GetDefaultObject<U4MLAgentElement>();
			if (CDO)
			{
				return FSTRING_TO_STD(CDO->GetDescription());
			}
		}
		rpc::this_handler().respond_error(std::make_tuple("Unable to find class", ClassName));
		return std::string("Unable to find class");
	}));

	//----------------------------------------------------------------------//
	// review end 
	//----------------------------------------------------------------------//

#endif // WITH_RPCLIB

	if (Session)
	{
		Session->ConfigureAsClient();
	}
	OnAddClientFunctions.Broadcast();
}