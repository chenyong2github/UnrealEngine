// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMetadata.h"

struct FShaderCompilerInput;
struct FShaderCompilerOutput;

/** Validates and moves all the shader loose data parameter defined in the root scope of the shader into the root uniform buffer. */
class SHADERCOMPILERCOMMON_API FShaderParameterParser
{
public:
	struct FParsedShaderParameter
	{
	public:
		/** Original information about the member. */
		const FShaderParametersMetadata::FMember* Member = nullptr;

		/** Information found about the member when parsing the preprocessed code. */
		FString ParsedName;
		FString ParsedType;
		FString ParsedArraySize;

		/** Offset the member should be in the constant buffer. */
		int32 ConstantBufferOffset = 0;

		/** Records if the parameter was fully moved. */
		bool bOriginalParameterErased{};

		/* Returns whether the shader parameter has been found when parsing. */
		bool IsFound() const
		{
			return !ParsedType.IsEmpty();
		}

		/** Returns whether the shader parameter is bindable to the shader parameter structure. */
		bool IsBindable() const
		{
			return Member != nullptr;
		}

	private:
		int32 ParsedPragmaLineoffset = 0;
		int32 ParsedLineOffset = 0;

		friend class FShaderParameterParser;
	};

	/** Parses the preprocessed shader code and move the parameters into root constant buffer */
	bool ParseAndMoveShaderParametersToRootConstantBuffer(
		const FShaderCompilerInput& CompilerInput,
		FShaderCompilerOutput& CompilerOutput,
		FString& PreprocessedShaderSource,
		const TCHAR* ConstantBufferType);

	/** Gets parsing information from a parameter binding name. */
	const FParsedShaderParameter& FindParameterInfos(const FString& ParameterName) const
	{
		return ParsedParameters.FindChecked(ParameterName);
	}

	/** Validates the shader parameter in code is compatible with the shader parameter structure. */
	void ValidateShaderParameterType(
		const FShaderCompilerInput& CompilerInput,
		const FString& ShaderBindingName,
		int32 ReflectionOffset,
		int32 ReflectionSize,
		bool bPlatformSupportsPrecisionModifier,
		FShaderCompilerOutput& CompilerOutput) const;

	void ValidateShaderParameterType(
		const FShaderCompilerInput& CompilerInput,
		const FString& ShaderBindingName,
		int32 ReflectionOffset,
		int32 ReflectionSize,
		FShaderCompilerOutput& CompilerOutput) const
	{
		ValidateShaderParameterType(CompilerInput, ShaderBindingName, ReflectionOffset, ReflectionSize, false, CompilerOutput);
	}

	/** Validates shader parameter map is compatible with the shader parameter structure. */
	void ValidateShaderParameterTypes(
		const FShaderCompilerInput& CompilerInput,
		bool bPlatformSupportsPrecisionModifier,
		FShaderCompilerOutput& CompilerOutput) const;

	void ValidateShaderParameterTypes(
		const FShaderCompilerInput& CompilerInput,
		FShaderCompilerOutput& CompilerOutput) const
	{
		ValidateShaderParameterTypes(CompilerInput, false, CompilerOutput);
	}

	/** Gets file and line of the parameter in the shader source code. */
	void GetParameterFileAndLine(const FParsedShaderParameter& ParsedParameter, FString& OutFile, FString& OutLine) const
	{
		return ExtractFileAndLine(ParsedParameter.ParsedPragmaLineoffset, ParsedParameter.ParsedLineOffset, OutFile, OutLine);
	}

private:
	void ExtractFileAndLine(int32 PragamLineoffset, int32 LineOffset, FString& OutFile, FString& OutLine) const;

	FString OriginalParsedShader;

	TMap<FString, FParsedShaderParameter> ParsedParameters;

	bool bMovedLoosedParametersToRootConstantBuffer = false;
};
