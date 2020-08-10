// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DirectLink/DeltaConsumer.h"
#include "DirectLink/DirectLinkCommon.h"

#include "Containers/UnrealString.h"
#include "CoreTypes.h"


namespace DirectLink
{

class ISceneProvider
{
public:
	enum ESceneStatus
	{
		None,
		CanCreateScene,
		SceneExists,
	};

public:
	virtual ~ISceneProvider() = default;
	virtual ESceneStatus GetSceneStatus(const FSceneIdentifier& SceneName) = 0;
	virtual TSharedPtr<IDeltaConsumer> GetDeltaConsumer(const FSceneIdentifier& Scene) = 0;
	virtual bool CanOpenNewConnection() = 0; // source info ?
};

} // namespace DirectLink

