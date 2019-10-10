// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "DatasmithVREDSceneProcessor.h"
#include "DatasmithVREDLog.h"
#include "DatasmithFBXScene.h"

FDatasmithVREDSceneProcessor::FDatasmithVREDSceneProcessor(FDatasmithFBXScene* InScene)
	: FDatasmithFBXSceneProcessor(InScene)
{
}

void FDatasmithVREDSceneProcessor::AddExtraLightInfo(TArray<FDatasmithFBXSceneLight>* InExtraLightsInfo)
{
	// Speed up lookups
	ExtraLightsInfo.Empty();
	for (FDatasmithFBXSceneLight l : *InExtraLightsInfo)
	{
		ExtraLightsInfo.Add(l.Name, MakeShared<FDatasmithFBXSceneLight>(l));
	}

	AddExtraLightNodesRecursive(Scene->RootNode);

	ExtraLightsInfo.Empty();
};

void FDatasmithVREDSceneProcessor::AddExtraLightNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node)
{
	TSharedPtr<FDatasmithFBXSceneLight>* ExtraInfo = ExtraLightsInfo.Find(Node->Name);
	if (ExtraInfo != nullptr)
	{
		// Take a deep copy from ExtraInfo
		Node->Light = TSharedPtr<FDatasmithFBXSceneLight>(new FDatasmithFBXSceneLight(**ExtraInfo));
		UE_LOG(LogDatasmithVREDImport, Log, TEXT("Adding extra info to light '%s'"), *Node->Name);
	}

	for (TSharedPtr<FDatasmithFBXSceneNode> Child : Node->Children)
	{
		AddExtraLightNodesRecursive(Child);
	}
}

void FDatasmithVREDSceneProcessor::AddMatsMaterials(TArray<FDatasmithFBXSceneMaterial>* InMatsMaterials)
{
	TMap<FString, FDatasmithFBXSceneMaterial*> ExistingMats;
	for (TSharedPtr<FDatasmithFBXSceneMaterial>& ExistingMat : Scene->Materials)
	{
		ExistingMats.Add(ExistingMat->Name, ExistingMat.Get());
	}

	for (const FDatasmithFBXSceneMaterial& InMat : *InMatsMaterials)
	{
		FDatasmithFBXSceneMaterial** FoundMat = ExistingMats.Find(InMat.Name);
		if (FoundMat)
		{
			**FoundMat = InMat;
		}
		else
		{
			TSharedPtr<FDatasmithFBXSceneMaterial> AddedMat = MakeShared<FDatasmithFBXSceneMaterial>();
			*AddedMat = InMat;
			Scene->Materials.Add(AddedMat);
		}
	}
}