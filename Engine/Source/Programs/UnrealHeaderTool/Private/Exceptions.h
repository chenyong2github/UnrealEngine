// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/UnrealString.h"
#include "Misc/CompilationResult.h"
#include "ProfilingDebugging/ScopedTimers.h"

class FBaseParser;
class FUnrealSourceFile;
class FUnrealTypeDefinitionInfo;

class FUHTExceptionContext
{
public:
	virtual FString GetFilename() const = 0;
	virtual int32 GetLineNumber() const = 0;
};

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
	 * @param InContext The context of the exception
	 * @param InText The text of the error
	 */
	UE_NORETURN static void Throwf(const FUHTExceptionContext& InContext, FString&& InText)
	{
		throw FUHTException(ECompilationResult::OtherCompilationError, InContext, MoveTemp(InText));
	}

	/**
	 * Generate an exception
	 * @param InContext The context of the exception
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(const FUHTExceptionContext& InContext, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(ECompilationResult::OtherCompilationError, InContext, MoveTemp(ResultString));
	}

	/**
	 * Generate an exception
	 * @param InResult Result code to be returned as the overall result of the compilation process
	 * @param InContext The context of the exception
	 * @param InFmt The format string
	 * @param InArgs Arguments supplied to the format string
	 */
	template <typename FmtType, typename... Types>
	UE_NORETURN static void VARARGS Throwf(ECompilationResult::Type InResult, const FUHTExceptionContext& InContext, const FmtType& InFmt, Types... InArgs)
	{
		FString ResultString = FString::Printf(InFmt, InArgs ...);
		throw FUHTException(InResult, InContext, MoveTemp(ResultString));
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
	FUHTException(ECompilationResult::Type InResult, const FUHTExceptionContext& InContext, FString&& InMessage);
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
	 * Mark that a warning has happened
	 */
	static void MarkWarning();

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
	 * Log an error for the given source file where the type is defined
	 * @param Context The context of the exception
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(const FUHTExceptionContext& Context, const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Log an error without any source file information
	 * @param ErrorMsg The text of the error
	 * @param Result Compilation result of the error
	 */
	static void LogError(const TCHAR* ErrorMsg, ECompilationResult::Type Result = ECompilationResult::OtherCompilationError);

	/**
	 * Log a warning.
	 * @param Filename The filename generating the warning.  If empty, then no file and line number are included in the warning.
	 * @param Line Line number of the warning
	 * @param Message Message body of the warning
	 * @param Result Compilation result of the warning
	 */
	static void LogWarning(FString&& Filename, int32 Line, const FString& Message);

	/**
	 * Log a warning for the given source file where the type is defined
	 * @param Context The context of the warning
	 * @param ErrorMsg The text of the warning
	 * @param Result Compilation result of the warning
	 */
	static void LogWarning(const FUHTExceptionContext& Context, const TCHAR* ErrorMsg);

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
	 */
	template<typename Lambda>
	static void TryAlways(Lambda&& InLambda)
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

#define UE_LOG_WARNING_UHT(Context, Format, ...) { FResults::LogWarning(Context, *FString::Printf(Format, ##__VA_ARGS__)); }
#define UE_LOG_ERROR_UHT(Context, Format, ...) { FResults::LogError(Context, *FString::Printf(Format, ##__VA_ARGS__)); }
