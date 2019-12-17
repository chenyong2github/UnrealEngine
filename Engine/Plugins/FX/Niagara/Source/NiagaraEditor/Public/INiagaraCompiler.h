// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraEditorCommon.h"
#include "NiagaraParameters.h"
#include "NiagaraScript.h"

class Error;
class UEdGraphPin;
class UNiagaraNode;
class UNiagaraGraph;
class UEdGraphPin;
class FCompilerResultsLog;
class UNiagaraDataInterface;
struct FNiagaraTranslatorOutput;
struct FNiagaraVMExecutableData;
class FNiagaraCompileRequestData;
class FNiagaraCompileOptions;
//struct FNiagaraCompileEvent;

/** Defines information about the results of a Niagara script compile. */
struct FNiagaraCompileResults
{
	/** Whether or not the script compiled successfully for VectorVM */
	bool bVMSucceeded;

	/** Whether or not the script compiled successfully for GPU compute */
	bool bComputeSucceeded;
	
	/** The actual final compiled data.*/
	TSharedPtr<FNiagaraVMExecutableData> Data;

	float CompileTime;

	/** Tracking any compilation warnings or errors that occur.*/
	TArray<FNiagaraCompileEvent> CompileEvents;
	uint32 NumErrors;
	uint32 NumWarnings;

	FString DumpDebugInfoPath;

	FNiagaraCompileResults()
		: CompileTime(0.0f), NumErrors(0), NumWarnings(0)
	{
	}

	static ENiagaraScriptCompileStatus CompileResultsToSummary(const FNiagaraCompileResults* CompileResults);
};

//Interface for Niagara compilers.
// NOTE: the graph->hlsl translation step is now in FNiagaraHlslTranslator
//
class INiagaraCompiler
{
public:
	/** Starts the async compilation of a script and returns the job handle to retrieve the results */
	virtual int32 CompileScript(const FNiagaraCompileRequestData* InCompileRequest, const FNiagaraCompileOptions& InOptions, FNiagaraTranslatorOutput *TranslatorOutput, FString& TranslatedHLSL) = 0;

	/** Returns the compile result for a given job id once the job has finished compiling. */
	virtual TOptional<FNiagaraCompileResults> GetCompileResult(int32 JobID, bool bWait = false) = 0;

	/** Adds an error to be reported to the user. Any error will lead to compilation failure. */
	virtual void Error(FText ErrorText) = 0 ;

	/** Adds a warning to be reported to the user. Warnings will not cause a compilation failure. */
	virtual void Warning(FText WarningText) = 0;
};
