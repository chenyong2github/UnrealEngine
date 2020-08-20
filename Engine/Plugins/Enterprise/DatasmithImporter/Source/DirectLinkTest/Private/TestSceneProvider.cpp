// Copyright Epic Games, Inc. All Rights Reserved.

#include "TestSceneProvider.h"
#include "DirectLink/DatasmithDeltaConsumer.h"

FTestSceneProvider::ESceneStatus FTestSceneProvider::GetSceneStatus(const DirectLink::FSceneIdentifier& SceneName)
{
	return ESceneStatus::CanCreateScene;
}

TSharedPtr<DirectLink::IDeltaConsumer> FTestSceneProvider::GetDeltaConsumer(const DirectLink::FSceneIdentifier& Scene)
{
	if (const auto* ElementPtr = Consumers.Find(Scene.SceneGuid))
	{
		return *ElementPtr;
	}

	return Consumers.Add(Scene.SceneGuid, MakeShared<FDatasmithDeltaConsumer>());
}

bool FTestSceneProvider::CanOpenNewConnection()
{
	static bool tmp = true;
	return tmp;
}

