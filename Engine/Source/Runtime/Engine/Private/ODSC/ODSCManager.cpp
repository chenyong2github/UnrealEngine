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
	: FTickerObjectBase(0.0f, FBackgroundableTicker::GetCoreTicker())
	, Thread(new FODSCThread())
{
	Thread->StartThread();
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
			ProcessCookOnTheFlyShaders(CompletedRequest->ReloadGlobalShaders(), CompletedRequest->GetMeshMaterialMaps(), CompletedRequest->GetMaterialsToLoad());
			delete CompletedRequest;
		}
	}

	// keep ticking
	return true;
}

void FODSCManager::AddThreadedRequest(const TArray<FString>& MaterialsToCompile, EShaderPlatform ShaderPlatform, bool bCompileChangedShaders)
{
	check(Thread);
	Thread->AddRequest(MaterialsToCompile, ShaderPlatform, bCompileChangedShaders);
}

void FODSCManager::AddThreadedShaderPipelineRequest(EShaderPlatform ShaderPlatform, const FString& MaterialName, const FString& VertexFactoryName, const FString& PipelineName, const TArray<FString>& ShaderTypeNames)
{
	check(Thread);
	Thread->AddShaderPipelineRequest(ShaderPlatform, MaterialName, VertexFactoryName, PipelineName, ShaderTypeNames);
}
