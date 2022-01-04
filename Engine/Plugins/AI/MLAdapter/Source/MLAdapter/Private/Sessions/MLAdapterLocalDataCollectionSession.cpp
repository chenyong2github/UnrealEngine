// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sessions/MLAdapterLocalDataCollectionSession.h"
#include "Agents/MLAdapterAgent.h"
#include "MLAdapterTypes.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

void UMLAdapterLocalDataCollectionSession::OnPostWorldInit(UWorld& World)
{
	Super::OnPostWorldInit(World);

	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		// This will be cleaned up in UMLAdapterLocalDataCollectionSession::Close()
		GameInstance->GetOnPawnControllerChanged().AddDynamic(this, &UMLAdapterLocalDataCollectionSession::OnPawnControllerChanged);
	}
}

void UMLAdapterLocalDataCollectionSession::OnPawnControllerChanged(APawn* InPawn, AController* InController)
{
	for (UMLAdapterAgent* Agent : Agents)
	{
		if (Agent->GetController() == InController)
		{
			PlayerControlledAgent = Agent;
			break;
		}
	}
}

void UMLAdapterLocalDataCollectionSession::Tick(float DeltaTime)
{
	UMLAdapterAgent* Agent = PlayerControlledAgent.Get();

	if (Agent ==  nullptr)
	{
		UE_LOG(LogUnrealEditorMLAdapter, Log, TEXT("LocalDataCollectionSession: Player-controlled agent not found yet."))
		return;
	}

	Agent->Sense(DeltaTime);
	TArray<uint8> Buffer;
	FMLAdapterMemoryWriter Writer(Buffer);
	Agent->GetObservations(Writer);

	FFileHelper::SaveArrayToFile(Buffer, *FileName, &IFileManager::Get(), EFileWrite::FILEWRITE_Append);
}

void UMLAdapterLocalDataCollectionSession::Close()
{
	Super::Close();

	if (CachedWorld && CachedWorld->GetGameInstance())
	{
		CachedWorld->GetGameInstance()->GetOnPawnControllerChanged().RemoveAll(this);
	}
}
