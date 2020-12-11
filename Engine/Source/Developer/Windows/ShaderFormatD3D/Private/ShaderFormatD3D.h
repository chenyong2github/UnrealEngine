// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Windows/WindowsHWrapper.h"

struct FShaderTarget;

enum class ELanguage
{
	SM5,
	ES3_1,
};

void CompileShader_Windows(const struct FShaderCompilerInput& Input,struct FShaderCompilerOutput& Output,const class FString& WorkingDirectory, ELanguage Language);

bool CompileAndProcessD3DShaderFXC(FString& PreprocessedShaderSource, const FString& CompilerPath,
	uint32 CompileFlags, const FShaderCompilerInput& Input, FString& EntryPointName,
	const TCHAR* ShaderProfile, bool bProcessingSecondTime,
	TArray<FString>& FilteredErrors, FShaderCompilerOutput& Output);

bool CompileAndProcessD3DShaderDXC(FString& PreprocessedShaderSource,
	uint32 CompileFlags, const FShaderCompilerInput& Input, FString& EntryPointName,
	const TCHAR* ShaderProfile, ELanguage Language, bool bProcessingSecondTime,
	TArray<FString>& FilteredErrors, FShaderCompilerOutput& Output);

bool ValidateResourceCounts(uint32 NumSRVs, uint32 NumSamplers, uint32 NumUAVs, uint32 NumCBs, TArray<FString>& OutFilteredErrors);
bool DumpDebugShaderUSF(FString& PreprocessedShaderSource, const FShaderCompilerInput& Input);

extern int32 GD3DAllowRemoveUnused;
