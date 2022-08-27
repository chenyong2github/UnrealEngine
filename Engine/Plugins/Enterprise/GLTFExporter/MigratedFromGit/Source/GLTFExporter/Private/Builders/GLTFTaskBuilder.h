// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBuilder.h"
#include "Builders/GLTFTask.h"
#include "GLTFExportOptions.h"

class FGLTFTaskBuilder : public FGLTFBuilder
{
protected:

	FGLTFTaskBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

	template <typename TaskType, typename = typename TEnableIf<TIsDerivedFrom<TaskType, FGLTFTask>::Value>::Type>
	void SetupTask(TUniquePtr<TaskType> Task)
	{
		SetupTask(TUniquePtr<FGLTFTask>(Task.Release()));
	}

	void SetupTask(TUniquePtr<FGLTFTask> Task);

	void CompleteAllTasks(FFeedbackContext* Context = GWarn);

private:

	static FText GetCategoryFormatMessage(EGLTFTaskCategory Category);

	TMap<EGLTFTaskCategory, TArray<TUniquePtr<FGLTFTask>>> CategorizedTasks;
};
