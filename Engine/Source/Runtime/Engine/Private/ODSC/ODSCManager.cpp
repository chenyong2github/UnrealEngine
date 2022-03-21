// Copyright Epic Games, Inc. All Rights Reserved.

#include "ODSC/ODSCManager.h"
#include "ODSCLog.h"
#include "ODSCThread.h"
#include "Containers/BackgroundableTicker.h"
#include "EngineModule.h"
#include "ShaderCompiler.h"

DEFINE_LOG_CATEGORY(LogODSC);

// FODSCManager

FODSCManager* GODSCManager = nullptr;

FODSCManager::FODSCManager()
	: FTSTickerObjectBase(0.0f, FTSBackgroundableTicker::GetCoreTicker())
{
	if (IsRunningCookOnTheFly())
	{
		Thread = new FODSCThread();
		Thread->StartThread();
	}
}

FODSCManager::~FODSCManager()
{
	if (Thread)
	{
		Thread->StopThread();
		delete Thread;
	}
}

bool FODSCManager::Tick(float DeltaSeconds)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FODSCManager_Tick);

	if (Thread)
	{
		Thread->Wakeup();

		TArray<FODSCMessageHandler*> CompletedThreadedRequests;
		Thread->GetCompletedRequests(CompletedThreadedRequests);

		// Finish and remove any completed requests
		for (FODSCMessageHandler* CompletedRequest : CompletedThreadedRequests)
		{
			check(CompletedRequest);
			ProcessCookOnTheFlyShaders(false, CompletedRequest->GetMeshMaterialMaps(), CompletedRequest->GetMaterialsToLoad(), CompletedRequest->GetGlobalShaderMap());
			delete CompletedRequest;
		}
	}

	// keep ticking
	return true;
}

void FODSCManager::AddThreadedRequest(const TArray<FString>& MaterialsToCompile, EShaderPlatform ShaderPlatform, ODSCRecompileCommand RecompileCommandType)
{
	if (IsRunningCookOnTheFly())
	{
		check(Thread);
		Thread->AddRequest(MaterialsToCompile, ShaderPlatform, RecompileCommandType);
	}
}

void FODSCManager::AddThreadedShaderPipelineRequest(EShaderPlatform ShaderPlatform, const FString& MaterialName, const FString& VertexFactoryName, const FString& PipelineName, const TArray<FString>& ShaderTypeNames)
{
	if (IsRunningCookOnTheFly())
	{
		check(Thread);
		Thread->AddShaderPipelineRequest(ShaderPlatform, MaterialName, VertexFactoryName, PipelineName, ShaderTypeNames);
	}
}
