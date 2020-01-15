// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderCore.h"

/**
 * Preprocess a shader.
 * @param OutPreprocessedShader - Upon return contains the preprocessed source code.
 * @param ShaderOutput - ShaderOutput to which errors can be added.
 * @param ShaderInput - The shader compiler input.
 * @param AdditionalDefines - Additional defines with which to preprocess the shader.
 * @param bShaderDumpDefinesAsCommentedCode - Whether to add shader definitions as comments.
 * @returns true if the shader is preprocessed without error.
 */
extern SHADERPREPROCESSOR_API bool PreprocessShader(
	FString& OutPreprocessedShader,
	FShaderCompilerOutput& ShaderOutput,
	const FShaderCompilerInput& ShaderInput,
	const FShaderCompilerDefinitions& AdditionalDefines,
	bool bShaderDumpDefinesAsCommentedCode = true);
