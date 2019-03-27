// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleInterface.h"

struct FPythonCommandEx;

class IPythonScriptPlugin : public IModuleInterface
{
public:
	/** Get this module */
	static IPythonScriptPlugin* Get()
	{
		static const FName ModuleName = "PythonScriptPlugin";
		return FModuleManager::GetModulePtr<IPythonScriptPlugin>(ModuleName);
	}

	/**
	 * Check to see whether the plugin was built with Python support enabled.
	 */
	virtual bool IsPythonAvailable() const = 0;

	/**
	 * Execute the given Python command.
	 * This may be literal Python code, or a file (with optional arguments) that you want to run.
	 * @return true if the command ran successfully, false if there were errors (the output log will show the errors).
	 */
	virtual bool ExecPythonCommand(const TCHAR* InPythonCommand) = 0;

	/**
	 * Execute the given Python command.
	 * @return true if the command ran successfully, false if there were errors.
	 */
	virtual bool ExecPythonCommandEx(FPythonCommandEx& InOutPythonCommand) = 0;
	
	/**
	 * Delegate called after Python has been initialized.
	 */
	virtual FSimpleMulticastDelegate& OnPythonInitialized() = 0;

	/**
	 * Delegate called before Python is shutdown.
	 */
	virtual FSimpleMulticastDelegate& OnPythonShutdown() = 0;
};

/** Types of log output that Python can give. */
enum class EPythonLogOutputType : uint8
{
	/** This log was informative. */
	Info,
	/** This log was a warning. */
	Warning,
	/** This log was an error. */
	Error,
};

/** Flags that can be specified when running Python commands. */
enum class EPythonCommandFlags : uint8
{
	/** No special behavior. */
	None = 0,
	/** Run the Python command in "unattended" mode (GIsRunningUnattendedScript set to true), which will suppress certain pieces of UI. */
	Unattended = 1<<0,
};
ENUM_CLASS_FLAGS(EPythonCommandFlags);

/** Controls the execution mode used for the Python command. */
enum class EPythonCommandExecutionMode : uint8
{
	/** Execute the Python command as a file. This allows you to execute either a literal Python script containing multiple statements, or a file with optional arguments. */
	ExecuteFile,
	/** Execute the Python command as a single statement. This will execute a single statement and print the result. This mode cannot run files. */
	ExecuteStatement,
	/** Evaluate the Python command as a single statement. This will evaluate a single statement and return the result. This mode cannot run files. */
	EvaluateStatement,
};

/** Log output entry captured from Python. */
struct FPythonLogOutputEntry
{
	/** The type of the log output. */
	EPythonLogOutputType Type = EPythonLogOutputType::Info;

	/** The log output string. */
	FString Output;
};

/** Extended information when executing Python commands. */
struct FPythonCommandEx
{
	/** Flags controlling how the command should be run. */
	EPythonCommandFlags Flags = EPythonCommandFlags::None;

	/** Controls the mode used to execute the command. */
	EPythonCommandExecutionMode ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;

	/** The command to run. This may be literal Python code, or a file (with optional arguments) to run. */
	FString Command;

	/** The result of running the command. On success, for EvaluateStatement mode this will be the actual result of running the command, and will be None in all other cases. On failure, this will be the error information (typically a Python exception trace). */
	FString CommandResult;

	/** The log output captured while running the command. */
	TArray<FPythonLogOutputEntry> LogOutput;
};

inline const TCHAR* LexToString(EPythonLogOutputType InType)
{
	switch (InType)
	{
	case EPythonLogOutputType::Info:
		return TEXT("Info");
	case EPythonLogOutputType::Warning:
		return TEXT("Warning");
	case EPythonLogOutputType::Error:
		return TEXT("Error");
	default:
		break;
	}
	return TEXT("<Unknown EPythonLogOutputType>");
}

inline const TCHAR* LexToString(EPythonCommandExecutionMode InMode)
{
	switch (InMode)
	{
	case EPythonCommandExecutionMode::ExecuteFile:
		return TEXT("ExecuteFile");
	case EPythonCommandExecutionMode::ExecuteStatement:
		return TEXT("ExecuteStatement");
	case EPythonCommandExecutionMode::EvaluateStatement:
		return TEXT("EvaluateStatement");
	default:
		break;
	}
	return TEXT("<Unknown EPythonCommandExecutionMode>");
}

inline bool LexTryParseString(EPythonCommandExecutionMode& OutMode, const TCHAR* InBuffer)
{
	if (FCString::Stricmp(InBuffer, TEXT("ExecuteFile")) == 0)
	{
		OutMode = EPythonCommandExecutionMode::ExecuteFile;
		return true;
	}
	if (FCString::Stricmp(InBuffer, TEXT("ExecuteStatement")) == 0)
	{
		OutMode = EPythonCommandExecutionMode::ExecuteStatement;
		return true;
	}
	if (FCString::Stricmp(InBuffer, TEXT("EvaluateStatement")) == 0)
	{
		OutMode = EPythonCommandExecutionMode::EvaluateStatement;
		return true;
	}
	return false;
}

inline void LexFromString(EPythonCommandExecutionMode& OutMode, const TCHAR* InBuffer)
{
	OutMode = EPythonCommandExecutionMode::ExecuteFile;
	LexTryParseString(OutMode, InBuffer);
}
