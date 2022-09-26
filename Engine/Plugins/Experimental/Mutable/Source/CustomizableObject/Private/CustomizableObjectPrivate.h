// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableRuntime/Public/Model.h"
#include "MutableRuntime/Public/Serialisation.h"



class FCustomizableObjectPrivateData
{
private:

	mu::ModelPtr MutableModel;

public:

	void SetModel(mu::Model* Model);
	mu::Model* GetModel();
	const mu::Model* GetModel() const;

	// See UCustomizableObjectSystem::LockObject.
	bool bLocked = false;

#if WITH_EDITOR
	bool bModelCompiledForCook = false;

	TArray<FString> CachedPlatformNames;
#endif

};

