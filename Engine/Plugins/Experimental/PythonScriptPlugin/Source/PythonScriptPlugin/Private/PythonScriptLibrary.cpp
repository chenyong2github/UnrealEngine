// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PythonScriptLibrary.h"
#include "IPythonScriptPlugin.h"

bool UPythonScriptLibrary::IsPythonAvailable()
{
	return IPythonScriptPlugin::Get()->IsPythonAvailable();
}

bool UPythonScriptLibrary::ExecutePythonCommand(const FString& PythonCommand)
{
	return IPythonScriptPlugin::Get()->ExecPythonCommand(*PythonCommand);
}

bool UPythonScriptLibrary::ExecutePythonCommandEx(const FString& PythonCommand, FString& CommandResult, TArray<FPythonLogOutputEntry>& LogOutput, const EPythonCommandExecutionMode ExecutionMode, const EPythonFileExecutionScope FileExecutionScope)
{
	FPythonCommandEx PythonCommandEx;
	PythonCommandEx.Command = PythonCommand;
	PythonCommandEx.ExecutionMode = ExecutionMode;
	PythonCommandEx.FileExecutionScope = FileExecutionScope;

	const bool bResult = IPythonScriptPlugin::Get()->ExecPythonCommandEx(PythonCommandEx);
		
	CommandResult = MoveTemp(PythonCommandEx.CommandResult);
	LogOutput = MoveTemp(PythonCommandEx.LogOutput);
		
	return bResult;
}
