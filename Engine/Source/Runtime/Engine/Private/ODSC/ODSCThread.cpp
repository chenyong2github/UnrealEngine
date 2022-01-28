// Copyright Epic Games, Inc. All Rights Reserved.

#include "ODSCThread.h"
#include "ODSCLog.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/RunnableThread.h"
#include "ShaderCompiler.h"

FODSCRequestPayload::FODSCRequestPayload(EShaderPlatform InShaderPlatform, const FString& InMaterialName, const FString& InVertexFactoryName, const FString& InPipelineName, const TArray<FString>& InShaderTypeNames, const FString& InRequestHash)
	: ShaderPlatform(InShaderPlatform), MaterialName(InMaterialName), VertexFactoryName(InVertexFactoryName), PipelineName(InPipelineName), ShaderTypeNames(std::move(InShaderTypeNames)), RequestHash(InRequestHash)
{

}

FODSCMessageHandler::FODSCMessageHandler(EShaderPlatform InShaderPlatform, ODSCRecompileCommand InRecompileCommandType) 
:	ShaderPlatform(InShaderPlatform),
	RecompileCommandType(InRecompileCommandType)
{
}

FODSCMessageHandler::FODSCMessageHandler(const TArray<FString>& InMaterials, EShaderPlatform InShaderPlatform, ODSCRecompileCommand InRecompileCommandType) :
	MaterialsToLoad(std::move(InMaterials)),
	ShaderPlatform(InShaderPlatform),
	RecompileCommandType(InRecompileCommandType)
{
}

void FODSCMessageHandler::FillPayload(FArchive& Payload)
{
	// When did we start this request?
	RequestStartTime = FPlatformTime::Seconds();

	Payload << MaterialsToLoad;
	uint32 ConvertedShaderPlatform = (uint32)ShaderPlatform;
	Payload << ConvertedShaderPlatform;
	Payload << RecompileCommandType;
	Payload << RequestBatch;
}

void FODSCMessageHandler::ProcessResponse(FArchive& Response)
{
	UE_LOG(LogODSC, Display, TEXT("Received response in %lf seconds."), FPlatformTime::Seconds() - RequestStartTime);

	// pull back the compiled mesh material data (if any)
	Response << OutMeshMaterialMaps;
	Response << OutGlobalShaderMap;
}

void FODSCMessageHandler::AddPayload(const FODSCRequestPayload& Payload)
{
	RequestBatch.Add(Payload);
}

const TArray<FString>& FODSCMessageHandler::GetMaterialsToLoad() const
{
	return MaterialsToLoad;
}

const TArray<uint8>& FODSCMessageHandler::GetMeshMaterialMaps() const
{
	return OutMeshMaterialMaps;
}

const TArray<uint8>& FODSCMessageHandler::GetGlobalShaderMap() const
{
	return OutGlobalShaderMap;
}

bool FODSCMessageHandler::ReloadGlobalShaders() const
{
	return RecompileCommandType == ODSCRecompileCommand::Global;
}

FODSCThread::FODSCThread()
	: Thread(nullptr),
	  WakeupEvent(FPlatformProcess::GetSynchEventFromPool(true))
{
	UE_LOG(LogODSC, Log, TEXT("ODSC Thread active."));
}

FODSCThread::~FODSCThread()
{
	StopThread();

	FPlatformProcess::ReturnSynchEventToPool(WakeupEvent);
	WakeupEvent = nullptr;
}

void FODSCThread::StartThread()
{
	Thread = FRunnableThread::Create(this, TEXT("ODSCThread"), 128 * 1024, TPri_Normal);
}

void FODSCThread::StopThread()
{
	if (Thread != nullptr)
	{
		Thread->Kill(true);
		delete Thread;
		Thread = nullptr;
	}
}

void FODSCThread::Tick()
{
	Process();
}

void FODSCThread::AddRequest(const TArray<FString>& MaterialsToCompile, EShaderPlatform ShaderPlatform, ODSCRecompileCommand RecompileCommandType)
{
	PendingMaterialThreadedRequests.Enqueue(new FODSCMessageHandler(MaterialsToCompile, ShaderPlatform, RecompileCommandType));
}

void FODSCThread::AddShaderPipelineRequest(EShaderPlatform ShaderPlatform, const FString& MaterialName, const FString& VertexFactoryName, const FString& PipelineName, const TArray<FString>& ShaderTypeNames)
{
	FString RequestString = (MaterialName + VertexFactoryName + PipelineName);
	for (const auto& ShaderTypeName : ShaderTypeNames)
	{
		RequestString += ShaderTypeName;
	}
	const FString RequestHash = FMD5::HashAnsiString(*RequestString);

	FScopeLock Lock(&RequestHashCriticalSection);
	if (!RequestHashes.Contains(RequestHash))
	{
		PendingMeshMaterialThreadedRequests.Enqueue(FODSCRequestPayload(ShaderPlatform, MaterialName, VertexFactoryName, PipelineName, ShaderTypeNames, RequestHash));
		RequestHashes.Add(RequestHash);
	}
}

void FODSCThread::GetCompletedRequests(TArray<FODSCMessageHandler*>& OutCompletedRequests)
{
	check(IsInGameThread());
	FODSCMessageHandler* Request = nullptr;
	while (CompletedThreadedRequests.Dequeue(Request))
	{
		OutCompletedRequests.Add(Request);
	}
}

void FODSCThread::Wakeup()
{
	WakeupEvent->Trigger();
}

bool FODSCThread::Init()
{
	return true;
}

uint32 FODSCThread::Run()
{
	while (!ExitRequest.GetValue())
	{
		if (WakeupEvent->Wait())
		{
			Process();
		}
	}
	return 0;
}

void FODSCThread::Stop()
{
	ExitRequest.Set(true);
	WakeupEvent->Trigger();
}

void FODSCThread::Exit()
{

}

void FODSCThread::Process()
{
	// cache all pending requests.
	FODSCRequestPayload Payload;
	TArray<FODSCRequestPayload> PayloadsToAggregate;
	{
		FScopeLock Lock(&RequestHashCriticalSection);
		while (PendingMeshMaterialThreadedRequests.Dequeue(Payload))
		{
			PayloadsToAggregate.Add(Payload);
			int FoundIndex = INDEX_NONE;
			if (RequestHashes.Find(Payload.RequestHash, FoundIndex))
			{
				RequestHashes.RemoveAt(FoundIndex);
			}
		}
	}

	// cache material requests.
	FODSCMessageHandler* Request = nullptr;
	TArray<FODSCMessageHandler*> RequestsToStart;
	while (PendingMaterialThreadedRequests.Dequeue(Request))
	{
		RequestsToStart.Add(Request);
	}

	// process any material or recompile change shader requests or global shader compile requests.
	for (FODSCMessageHandler* NextRequest : RequestsToStart)
	{
		// send the info, the handler will process the response (and update shaders, etc)
		IFileManager::Get().SendMessageToServer(TEXT("RecompileShaders"), NextRequest);

		CompletedThreadedRequests.Enqueue(NextRequest);
	}

	// process any specific mesh material shader requests.
	if (PayloadsToAggregate.Num())
	{
		FODSCMessageHandler* RequestHandler = new FODSCMessageHandler(PayloadsToAggregate[0].ShaderPlatform, ODSCRecompileCommand::Material);
		for (const FODSCRequestPayload& payload : PayloadsToAggregate)
		{
			RequestHandler->AddPayload(payload);
		}

		// send the info, the handler will process the response (and update shaders, etc)
		IFileManager::Get().SendMessageToServer(TEXT("RecompileShaders"), RequestHandler);

		CompletedThreadedRequests.Enqueue(RequestHandler);
	}

	WakeupEvent->Reset();
}
