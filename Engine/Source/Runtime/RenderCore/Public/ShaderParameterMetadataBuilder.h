// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ShaderParameterMetadata.h: Meta data about shader parameter structures
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"

class RENDERCORE_API FShaderParametersMetadataBuilder
{
public:
	template<typename T>
	void AddParam(
		const TCHAR* Name,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		)
	{
		using TParamTypeInfo = TShaderParameterTypeInfo<T>;

		NextMemberOffset = Align(NextMemberOffset, TParamTypeInfo::Alignment);

		new(Members) FShaderParametersMetadata::FMember(
			Name,
			TEXT(""),
			__LINE__,
			NextMemberOffset,
			TParamTypeInfo::BaseType,
			Precision,
			TParamTypeInfo::NumRows,
			TParamTypeInfo::NumColumns,
			TParamTypeInfo::NumElements,
			TParamTypeInfo::GetStructMetadata()
			);

		NextMemberOffset += sizeof(typename TParamTypeInfo::TAlignedType);
	}

	void AddRDGBufferSRV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	void AddRDGBufferUAV(
		const TCHAR* Name,
		const TCHAR* ShaderType,
		EShaderPrecisionModifier::Type Precision = EShaderPrecisionModifier::Float
		);

	FShaderParametersMetadata* Build(
		FShaderParametersMetadata::EUseCase UseCase,
		const TCHAR* ShaderParameterName
		);

private:
	TArray<FShaderParametersMetadata::FMember> Members;
	uint32 NextMemberOffset = 0;
};