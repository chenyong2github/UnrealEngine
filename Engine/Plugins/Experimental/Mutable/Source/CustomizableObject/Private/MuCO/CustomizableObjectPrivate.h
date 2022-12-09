// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Model.h"
#include "MuR/Serialisation.h"



class FCustomizableObjectPrivateData
{
private:

	TSharedPtr<mu::Model, ESPMode::ThreadSafe> MutableModel;

public:

	void SetModel(const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& Model);
	const TSharedPtr<mu::Model, ESPMode::ThreadSafe>& GetModel();
	TSharedPtr<const mu::Model, ESPMode::ThreadSafe> GetModel() const;

	// See UCustomizableObjectSystem::LockObject. Must only be modified from the game thread
	bool bLocked = false;

#if WITH_EDITOR
	bool bModelCompiledForCook = false;

	TArray<FString> CachedPlatformNames;
#endif

};

