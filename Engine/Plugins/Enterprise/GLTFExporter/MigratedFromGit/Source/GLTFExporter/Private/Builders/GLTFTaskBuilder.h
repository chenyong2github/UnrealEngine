// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFLogBuilder.h"
#include "Tasks/GLTFTask.h"

class FGLTFTaskBuilder : public FGLTFLogBuilder
{
public:

	FGLTFTaskBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

	template <typename TaskType, typename... TaskArgTypes, typename = typename TEnableIf<TIsDerivedFrom<TaskType, FGLTFTask>::Value>::Type>
	bool SetupTask(TaskArgTypes&&... Args)
	{
		return SetupTask(MakeUnique<TaskType>(Forward<TaskArgTypes>(Args)...));
	}

	template <typename TaskType, typename = typename TEnableIf<TIsDerivedFrom<TaskType, FGLTFTask>::Value>::Type>
	bool SetupTask(TUniquePtr<TaskType> Task)
	{
		return SetupTask(TUniquePtr<FGLTFTask>(Task.Release()));
	}

	bool SetupTask(TUniquePtr<FGLTFTask> Task);

	void CompleteAllTasks(FFeedbackContext* Context = GWarn);

private:

	static FText GetPriorityMessageFormat(EGLTFTaskPriority Priority);

	int32 PriorityIndexLock;
	TMap<EGLTFTaskPriority, TArray<TUniquePtr<FGLTFTask>>> TasksByPriority;
};
