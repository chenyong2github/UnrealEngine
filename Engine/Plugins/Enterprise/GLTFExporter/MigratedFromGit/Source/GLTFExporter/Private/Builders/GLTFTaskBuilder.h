// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Builders/GLTFBuilder.h"
#include "Builders/GLTFTask.h"

class FGLTFTaskBuilder : public FGLTFBuilder
{
protected:

	FGLTFTaskBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions);

public:

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
