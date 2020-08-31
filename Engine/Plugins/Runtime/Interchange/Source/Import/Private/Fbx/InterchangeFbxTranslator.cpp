// Copyright Epic Games, Inc. All Rights Reserved. 
#include "Fbx/InterchangeFbxTranslator.h"

#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "InterchangeDispatcher.h"
#include "InterchangeDispatcherTask.h"
#include "LogInterchangeImportPlugin.h"
#include "Material/MaterialPayLoad.h"
#include "Mesh/SkeletalMeshPayload.h"
#include "Mesh/StaticMeshPayload.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Nodes/BaseNodeContainer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

UInterchangeFbxTranslator::UInterchangeFbxTranslator(const class FObjectInitializer& ObjectInitializer)
{
	Dispatcher = nullptr;
}

bool UInterchangeFbxTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
	const bool bIncludeDot = false;
	FString Extension = FPaths::GetExtension(InSourceData->GetFilename(), bIncludeDot);
	FString FbxExtension = (TEXT("fbx"));
	return FbxExtension.StartsWith(Extension,ESearchCase::IgnoreCase);
}

bool UInterchangeFbxTranslator::Translate(const UInterchangeSourceData* SourceData, Interchange::FBaseNodeContainer& BaseNodeContainer) const
{
	FString Filename = SourceData->GetFilename();
	if (!FPaths::FileExists(Filename))
	{
		return false;
	}

	if(!Dispatcher.IsValid())
	{
		//Dispatch an Interchange worker by using the InterchangeDispatcher
		//Build Result folder
		FGuid RandomGuid;
		FPlatformMisc::CreateGuid(RandomGuid);
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		const FString ProjectSavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		const FString RamdomGuidDir = RandomGuid.ToString(EGuidFormats::Base36Encoded);
		if (!PlatformFile.DirectoryExists(*ProjectSavedDir))
		{
			PlatformFile.CreateDirectory(*ProjectSavedDir);
		}
		const FString InterchangeDir = FPaths::Combine(ProjectSavedDir, TEXT("Interchange"));
		if (!PlatformFile.DirectoryExists(*InterchangeDir))
		{
			PlatformFile.CreateDirectory(*InterchangeDir);
		}
		const FString ResultFolder = FPaths::Combine(InterchangeDir, RamdomGuidDir);
		if (!PlatformFile.DirectoryExists(*ResultFolder))
		{
			PlatformFile.CreateDirectory(*ResultFolder);
		}

		//Create the dispatcher
		Dispatcher = MakeUnique<InterchangeDispatcher::FInterchangeDispatcher>(ResultFolder);

		if(ensure(Dispatcher.IsValid()))
		{
			Dispatcher->StartProcess();
		}
	}

	if(!Dispatcher.IsValid())
	{
		return false;
	}

	//Create a json command to read the fbx file
	FString JsonCommand = CreateLoadFbxFileCommand(SourceData->GetFilename());
	int32 TaskIndex = Dispatcher->AddTask(JsonCommand);

	//Blocking call until all tasks are executed
	Dispatcher->WaitAllTaskToCompleteExecution();
		
	InterchangeDispatcher::ETaskState TaskState;
	FString JsonResult;
	FString JSonMessages;
	Dispatcher->GetTaskState(TaskIndex, TaskState, JsonResult, JSonMessages);

	//TODO: Parse the JSonMessage and add the message to the interchange not yet develop error messaging

	if(TaskState != InterchangeDispatcher::ETaskState::ProcessOk)
	{
		return false;
	}
	//Grab the result file and fill the BaseNodeContainer
	InterchangeDispatcher::FJsonLoadSourceCmd::JsonResultParser ResultParser;
	ResultParser.FromJson(JsonResult);
	FString BaseNodeContainerFilename = ResultParser.GetResultFilename();

	//Parse the filename to fill the container

	return true;
}

void UInterchangeFbxTranslator::ImportFinish()
{
	if (Dispatcher.IsValid())
	{
		Dispatcher->TerminateProcess();
	}
}

TOptional<Interchange::FStaticMeshPayloadData> UInterchangeFbxTranslator::GetStaticMeshPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import Fbx, bad source data."));
		return TOptional<Interchange::FStaticMeshPayloadData>();
	}
	//Use the Source data has the key for the dispatcher and send a command to retrieve the staticmesh bulk data
	TOptional<Interchange::FStaticMeshPayloadData> PayloadData = Interchange::FStaticMeshPayloadData();
	return PayloadData;
}

TOptional<Interchange::FSkeletalMeshPayloadData> UInterchangeFbxTranslator::GetSkeletalMeshPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import Fbx, bad source data."));
		return TOptional<Interchange::FSkeletalMeshPayloadData>();
	}
	//Use the Source data has the key for the dispatcher and send a command to retrieve the skeletalmesh bulk data
	TOptional<Interchange::FSkeletalMeshPayloadData> PayloadData = Interchange::FSkeletalMeshPayloadData();
	return PayloadData;
}

TOptional<Interchange::FMaterialPayloadData> UInterchangeFbxTranslator::GetMaterialPayloadData(const UInterchangeSourceData* SourceData, const FString& PayLoadKey) const
{
	if (!SourceData)
	{
		UE_LOG(LogInterchangeImportPlugin, Error, TEXT("Failed to import Fbx, bad source data."));
		return TOptional<Interchange::FMaterialPayloadData>();
	}
	//Use the Source data has the key for the dispatcher and send a command to retrieve the material bulk data
	TOptional<Interchange::FMaterialPayloadData> PayloadData = Interchange::FMaterialPayloadData();
	return PayloadData;
}

FString UInterchangeFbxTranslator::CreateLoadFbxFileCommand(const FString& FbxFilePath) const
{
	InterchangeDispatcher::FJsonLoadSourceCmd LoadSourceCommand(TEXT("FBX"), FbxFilePath);
	return LoadSourceCommand.ToJson();
}