// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EGLTFTaskCategory
{
	Actor,
    Mesh,
    Animation,
    Material,
    Texture,
    MAX
};

class FGLTFTask
{
public:

	const EGLTFTaskCategory Category;

	const FString Name;

	FGLTFTask(EGLTFTaskCategory Category, const FString& Name)
		: Category(Category)
		, Name(Name)
	{
	}

	virtual ~FGLTFTask() = default;

	virtual void Run() = 0;
};

class FGLTFSleepTask : public FGLTFTask
{
public:

	FGLTFSleepTask(EGLTFTaskCategory Category, const FString& Name)
        : FGLTFTask(Category, Name)
	{
	}

	virtual void Run() override
	{
		FPlatformProcess::Sleep(0.5);
	}
};
