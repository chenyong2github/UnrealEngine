// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Misc/CompilationResult.h"
#include "ProfilingDebugging/ScopedTimers.h"

class FUnrealSourceFile;
class FUnrealTypeDefinitionInfo;

class FUHTException
{
public:

	/**
	 * Generate an exception
	 * @param InFilename The file generating the exception
	 * @param InLine The line number in the file
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(FString&& InFilename, int32 InLine, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(ECompilationResult::OtherCompilationError, MoveTemp(InFilename), InLine, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InResult Result code to be returned as the overall result of the compilation process
	 * @param InFilename The file generating the exception
	 * @param InLine The line number in the file
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(ECompilationResult::Type InResult, FString&& InFilename, int32 InLine, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(InResult, MoveTemp(InFilename), InLine, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InSourceFile The source file generating the exception
	 * @param InLine The line number in the file
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(const FUnrealSourceFile& InSourceFile, int32 InLine, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(ECompilationResult::OtherCompilationError, InSourceFile, InLine, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InResult Result code to be returned as the overall result of the compilation process
	 * @param InSourceFile The source file generating the exception
	 * @param InLine The line number in the file
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(ECompilationResult::Type InResult, const FUnrealSourceFile& InSourceFile, int32 InLine, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(InResult, InSourceFile, InLine, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InTypeDef The type definition generating the exception.  The filename and line number will be retrieved from the type definition if possible.
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(FUnrealTypeDefinitionInfo& InTypeDef, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(ECompilationResult::OtherCompilationError, InTypeDef, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InResult Result code to be returned as the overall result of the compilation process
	 * @param InTypeDef The type definition generating the exception.  The filename and line number will be retrieved from the type definition if possible.
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(ECompilationResult::Type InResult, FUnrealTypeDefinitionInfo& InTypeDef, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(InResult, InTypeDef, MoveTemp(ResultString));
	}

	/**
	 * Return the result code of the exception
	 */
	ECompilationResult::Type GetResult() const 
	{ 
		return Result; 
	}

	/**
	 * Return the filename of the exception.
	 */
	const TCHAR* GetFilename() const 
	{
		return *Filename; 
	}

	/**
	 * Return the line number in the file of the exception
	 */
	int32 GetLine() const
	{
		return Line;
	}

	/**
	 * Return the message of the exception
	 */
	const FString& GetMessage() const 
	{ 
		return Message; 
	}

private:
	ECompilationResult::Type Result = ECompilationResult::OtherCompilationError;
	FString Message;
	FString Filename;
	int32 Line;

	FUHTException(ECompilationResult::Type InResult, FString&& InFilename, int32 InLine, FString&& InMessage);
	FUHTException(ECompilationResult::Type InResult, const FUnrealSourceFile& SourceFile, int32 InLine, FString&& InMessage);
	FUHTException(ECompilationResult::Type InResult, FUnrealTypeDefinitionInfo& TypeDef, FString&& InMessage);
	FUHTException(ECompilationResult::Type InResult, const UObject* Object, FString&& InMessage);
};

/** Helper methods for working with exceptions and compilation results */
struct FResults
{

	/**
	 * Wait for any pending error tasks to complete.
	 * 
	 * When job threads are used to log errors or throw exceptions, those errors and exceptions are collected by the main game thread.
	 * After waiting for all the pending jobs to complete, invoke this method to ensure that all pending errors and exceptions have
	 * been collected.
	 */
	static void WaitForErrorTasks();

	/**
	 * Test to see if no errors or exceptions have been posted.
	 */
	static bool IsSucceeding();

	/**
	 * Set the overall results.
	 */
	static void SetResult(ECompilationResult::Type InResult);

	/**
	 * Get the current results without processing for overall result
	 */
	static ECompilationResult::Type GetResults();

	/**
	 * Get the overall results to be returned from compilation.
	 */
	static ECompilationResult::Type GetOverallResults();

	/**
	 * Log an error.
	 * @param Filename The filename generating the error.  If empty, then no file and line number are included in the error.
	 * @param Line Line number of the error
	 * @param Message Message body of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(FString&& Filename, int32 Line, const FString& Message, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Log an error from an exception with possible override of the source file.
	 * @param SourceFile The source file currently being processed.  If the exception does not include filename information, the source file will be used.
	 * @param Ex The exception generating the error.
	 */
	static void LogError(const FUnrealSourceFile& SourceFile, const FUHTException& Ex);

	/**
	 * Log an error from an exception.
	 * @param Ex The exception generating the error.
	 */
	static void LogError(const FUHTException& Ex);

	/**
	 * Log an error for the given source file
	 * @param SourceFile The source file generating the error
	 * @param Line The line number generating the error
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(const FUnrealSourceFile& SourceFile, int32 Line, const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Log an error for the given source file where the object is defined
	 * @param Object The source file and line number will be determined from the object
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(const UObject& Object, const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Log an error for the given source file where the type is defined
	 * @param InTypeDef The source file and line number will be determined from the type
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(FUnrealTypeDefinitionInfo& InTypeDef, const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);
	
	/**
	 * Log an error without any source file information
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Invoke the given lambda in a try block catching all supported exception types.
	 * @param SourceFile The source file being processed
	 * @param InLambda The code to be executed in the try block
	 */
	template<typename Lambda>
	static void Try(FUnrealSourceFile& SourceFile, Lambda&& InLambda)
	{
		if (IsSucceeding())
		{
#if !PLATFORM_EXCEPTIONS_DISABLED
			try
#endif
			{
				InLambda();
			}
#if !PLATFORM_EXCEPTIONS_DISABLED
			catch (const FUHTException& Ex)
			{
				LogError(SourceFile, Ex);
			}
			catch (const TCHAR* ErrorMsg)
			{
				LogError(SourceFile, 1, ErrorMsg);
			}
#endif
		}
	}

	/**
	 * Invoke the given lambda in a try block catching all supported exception types.
	 * @param InLambda The code to be executed in the try block
	 */
	template<typename Lambda>
	static void Try(Lambda&& InLambda)
	{
		if (IsSucceeding())
		{
#if !PLATFORM_EXCEPTIONS_DISABLED
			try
#endif
			{
				InLambda();
			}
#if !PLATFORM_EXCEPTIONS_DISABLED
			catch (const FUHTException& Ex)
			{
				LogError(Ex);
			}
			catch (const TCHAR* ErrorMsg)
			{
				LogError(ErrorMsg);
			}
#endif
		}
	}

	/**
	 * Invoke the given lambda in a try block catching all supported exception types.
	 * @param InLambda The code to be executed in the try block
	 * @return The time in seconds it took to execute the lambda
	 */
	template<typename Lambda>
	static double TimedTry(Lambda&& InLambda)
	{
		double DeltaTime = 0.0;
		{
			FScopedDurationTimer Timer(DeltaTime);
			Try(InLambda);
		}
		return DeltaTime;
	}
};
