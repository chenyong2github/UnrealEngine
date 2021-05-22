// Copyright Epic Games, Inc. All Rights Reserved.

#include "Exceptions.h"

#include "ClassMaps.h"
#include "UnrealHeaderTool.h"
#include "UnrealHeaderToolGlobals.h"
#include "UnrealSourceFile.h"
#include "UnrealTypeDefinitionInfo.h"

#include "CoreGlobals.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/FileManager.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopeLock.h"

#include <atomic>

FUHTException::FUHTException(ECompilationResult::Type InResult, FString&& InFilename, int32 InLine, FString&& InMessage)
	: Result(InResult)
	, Message(MoveTemp(InMessage))
	, Filename(MoveTemp(InFilename))
	, Line(InLine)
{
}

FUHTException::FUHTException(ECompilationResult::Type InResult, const FUnrealSourceFile& SourceFile, int32 InLine, FString&& InMessage)
	: Result(InResult)
	, Message(MoveTemp(InMessage))
	, Filename(SourceFile.GetFilename())
	, Line(InLine)
{
}

FUHTException::FUHTException(ECompilationResult::Type InResult, FUnrealTypeDefinitionInfo& TypeDef, FString&& InMessage)
	: Result(InResult)
	, Message(MoveTemp(InMessage))
	, Filename(TypeDef.GetUnrealSourceFile().GetFilename())
	, Line(TypeDef.GetLineNumber())
{
}

namespace UE::UnrealHeaderTool::Exceptions::Private
{
	std::atomic<ECompilationResult::Type> OverallResults = ECompilationResult::Succeeded;
	std::atomic<int32> NumFailures = 0;
	FGraphEventArray ErrorTasks;
	FCriticalSection ErrorTasksCS;

	void LogErrorInternal(ECompilationResult::Type InResult, const FString& Filename, int32 Line, const FString& Message)
	{
		TGuardValue<ELogTimes::Type> DisableLogTimes(GPrintLogTimes, ELogTimes::None);

		FString FormattedErrorMessage;
		if (Filename.IsEmpty())
		{
			FormattedErrorMessage = FString::Printf(TEXT("Error: %s\r\n"), *Message);
		}
		else
		{
			FormattedErrorMessage = FString::Printf(TEXT("%s(%d): Error: %s\r\n"), *Filename, Line, *Message);
		}

		UE_LOG(LogCompile, Log, TEXT("%s"), *FormattedErrorMessage);
		GWarn->Log(ELogVerbosity::Error, FormattedErrorMessage);

		FResults::SetResult(InResult);
	}
}

void FResults::LogError(FString&& Filename, int32 Line, const FString& Message, ECompilationResult::Type InResult/* = ECompilationResult::OtherCompilationError*/)
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	if (IsInGameThread())
	{
		LogErrorInternal(InResult, Filename, Line, Message);
	}
	else
	{
		auto LogExceptionTask = [Filename = MoveTemp(Filename), Line, Message, InResult]()
		{
			LogErrorInternal(InResult, Filename, Line, Message);
		};

		FGraphEventRef EventRef = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(LogExceptionTask), TStatId(), nullptr, ENamedThreads::GameThread);

		FScopeLock Lock(&ErrorTasksCS);
		ErrorTasks.Add(EventRef);
	}
}

void FResults::LogError(const FUHTException& Ex)
{
	FString AbsFilename;
	const TCHAR* Filename = Ex.GetFilename();
	if (Filename != nullptr)
	{
		AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(Ex.GetFilename());
	}
	LogError(MoveTemp(AbsFilename), Ex.GetLine(), Ex.GetMessage());
}

void FResults::LogError(const FUnrealSourceFile& SourceFile, const FUHTException& Ex)
{
	FString AbsFilename;
	const TCHAR* Filename = Ex.GetFilename();
	if (Filename == nullptr)
	{
		Filename = *SourceFile.GetFilename();
	}
	if (Filename != nullptr)
	{
		AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(Ex.GetFilename());
	}
	LogError(MoveTemp(AbsFilename), Ex.GetLine(), Ex.GetMessage());
}

void FResults::LogError(const FUnrealSourceFile& SourceFile, int32 Line, const TCHAR* ErrorMsg, ECompilationResult::Type InResult/* = ECompilationResult::OtherCompilationError*/)
{
	FString AbsFilename = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*SourceFile.GetFilename());
	LogError(MoveTemp(AbsFilename), Line, ErrorMsg, InResult);
}

void FResults::LogError(const UObject& Object, const TCHAR* ErrorMsg, ECompilationResult::Type InResult/* = ECompilationResult::OtherCompilationError*/)
{
	if (const UField* Field = Cast<UField>(&Object))
	{
		if (TSharedRef<FUnrealTypeDefinitionInfo>* TypeDef = GTypeDefinitionInfoMap.Find(Field))
		{
			LogError((*TypeDef)->GetUnrealSourceFile(), (*TypeDef)->GetLineNumber(), ErrorMsg, InResult);
		}
	}
	LogError(ErrorMsg, InResult);
}

void FResults::LogError(FUnrealTypeDefinitionInfo& InTypeDef, const TCHAR* ErrorMsg, ECompilationResult::Type InResult/* = ECompilationResult::OtherCompilationError*/)
{
	if (InTypeDef.HasSource())
	{
		LogError(InTypeDef.GetUnrealSourceFile(), InTypeDef.GetLineNumber(), ErrorMsg, InResult);
	}
	LogError(ErrorMsg, InResult);
}

void FResults::LogError(const TCHAR* ErrorMsg, ECompilationResult::Type InResult/* = ECompilationResult::OtherCompilationError*/)
{
	LogError(TEXT(""), 1, ErrorMsg, InResult);
}

void FResults::WaitForErrorTasks()
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	FGraphEventArray LocalExceptionTasks;
	{
		FScopeLock Lock(&ErrorTasksCS);
		LocalExceptionTasks = MoveTemp(ErrorTasks);
	}
	FTaskGraphInterface::Get().WaitUntilTasksComplete(LocalExceptionTasks);
}

bool FResults::IsSucceeding()
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	return OverallResults == ECompilationResult::Succeeded;
}

void FResults::SetResult(ECompilationResult::Type InResult)
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	checkf(InResult != ECompilationResult::Succeeded, TEXT("The results can't be set to succeeded."));
	OverallResults = InResult;
	++NumFailures;
}

ECompilationResult::Type FResults::GetResults()
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	return OverallResults;
}

ECompilationResult::Type FResults::GetOverallResults()
{
	using namespace UE::UnrealHeaderTool::Exceptions::Private;

	// For some legacy reason, we don't actually return the result
	if (OverallResults != ECompilationResult::Succeeded || NumFailures > 0)
	{
		return ECompilationResult::OtherCompilationError;
	}
	return OverallResults;
}
