// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFTaskBuilder.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopedSlowTask.h"

FGLTFTaskBuilder::FGLTFTaskBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FGLTFBuilder(FilePath, ExportOptions)
{
}

void FGLTFTaskBuilder::CompleteAllTasks(FFeedbackContext* Context)
{
	const int32 CategoryCount = static_cast<int32>(EGLTFTaskCategory::MAX);
	for (int32 CategoryIndex = 0; CategoryIndex < CategoryCount; CategoryIndex++)
	{
		const EGLTFTaskCategory Category = static_cast<EGLTFTaskCategory>(CategoryIndex);

		TArray<TUniquePtr<FGLTFTask>>* Tasks = CategorizedTasks.Find(Category);
		if (Tasks == nullptr)
		{
			continue;
		}

		const FText FormatMessage = GetCategoryFormatMessage(Category);
		FScopedSlowTask Progress(Tasks->Num(), FormatMessage, true, *Context);
		Progress.MakeDialog();

		for (TUniquePtr<FGLTFTask>& Task : *Tasks)
		{
			const FText Name = FText::FromString(Task->Name);
			const FText Message = FText::Format(FormatMessage, Name);
			Progress.EnterProgressFrame(1, Message);

			Task->Run();
		}
	}

	CategorizedTasks.Empty();
}

void FGLTFTaskBuilder::SetupTask(TUniquePtr<FGLTFTask> Task)
{
	const EGLTFTaskCategory Category = Task->Category;
	CategorizedTasks.FindOrAdd(Category).Add(MoveTemp(Task));
}

FText FGLTFTaskBuilder::GetCategoryFormatMessage(EGLTFTaskCategory Category)
{
	switch (Category)
	{
		case EGLTFTaskCategory::Actor:     return NSLOCTEXT("GLTFExporter", "ActorTaskMessage", "Actors... {0}");
		case EGLTFTaskCategory::Mesh:      return NSLOCTEXT("GLTFExporter", "ActorTaskMessage", "Meshes... {0}");
		case EGLTFTaskCategory::Animation: return NSLOCTEXT("GLTFExporter", "ActorTaskMessage", "Animations... {0}");
		case EGLTFTaskCategory::Material:  return NSLOCTEXT("GLTFExporter", "ActorTaskMessage", "Materials... {0}");
		case EGLTFTaskCategory::Texture:   return NSLOCTEXT("GLTFExporter", "ActorTaskMessage", "Textures... {0}");
		default:                           checkNoEntry();
	}

	return {};
}
