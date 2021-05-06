// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCompiledShaderKey.h: AGX RHI Compiled Shader Key.
=============================================================================*/

#pragma once

struct FAGXCompiledShaderKey
{
	FAGXCompiledShaderKey(uint32 InCodeSize, uint32 InCodeCRC, uint32 InConstants)
		: CodeSize(InCodeSize)
		, CodeCRC(InCodeCRC)
		, Constants(InConstants)
	{
		// VOID
	}

	friend bool operator ==(const FAGXCompiledShaderKey& A, const FAGXCompiledShaderKey& B)
	{
		return A.CodeSize == B.CodeSize && A.CodeCRC == B.CodeCRC && A.Constants == B.Constants;
	}

	friend uint32 GetTypeHash(const FAGXCompiledShaderKey &Key)
	{
		return HashCombine(HashCombine(GetTypeHash(Key.CodeSize), GetTypeHash(Key.CodeCRC)), GetTypeHash(Key.Constants));
	}

	uint32 CodeSize;
	uint32 CodeCRC;
	uint32 Constants;
};
