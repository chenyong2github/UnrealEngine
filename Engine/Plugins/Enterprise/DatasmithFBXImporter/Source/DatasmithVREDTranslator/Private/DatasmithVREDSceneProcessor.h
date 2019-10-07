// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "DatasmithFBXSceneProcessor.h"

struct FDatasmithFBXScene;
struct FDatasmithFBXSceneLight;
struct FDatasmithFBXSceneMaterial;
struct FDatasmithFBXSceneNode;

class FDatasmithVREDSceneProcessor : public FDatasmithFBXSceneProcessor
{
public:
	FDatasmithVREDSceneProcessor(FDatasmithFBXScene* InScene);

	/** Add the extra info to the corresponding light nodes in the hierarchy */
	void AddExtraLightInfo(TArray<FDatasmithFBXSceneLight>* InExtraLightsInfo);

	/** Recursively add missing info to lights nodes */
	void AddExtraLightNodesRecursive(TSharedPtr<FDatasmithFBXSceneNode> Node);

	/** Overwrite FBX imported materials with mats material parameters and info */
	void AddMatsMaterials(TArray<FDatasmithFBXSceneMaterial>* InMatsMaterials);

protected:
	TMap<FString, TSharedPtr<FDatasmithFBXSceneLight>> ExtraLightsInfo;
};