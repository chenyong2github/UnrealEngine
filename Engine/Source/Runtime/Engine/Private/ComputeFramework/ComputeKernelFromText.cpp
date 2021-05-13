// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelFromText.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeFramework.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

UComputeKernelFromText::UComputeKernelFromText()
{
	UniqueId = FGuid::NewGuid();
}

#if WITH_EDITOR

void UComputeKernelFromText::PostLoad()
{
	Super::PostLoad();
	ReparseKernelSourceText();
}

void UComputeKernelFromText::PreEditChange(FEditPropertyChain& PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);
	PrevSourceFile = SourceFile;
}

void UComputeKernelFromText::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* ModifiedProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	if (!ModifiedProperty)
	{
		return;
	}

	FName ModifiedPropName = ModifiedProperty->GetFName();

	if (ModifiedPropName == GET_MEMBER_NAME_CHECKED(UComputeKernelFromText, SourceFile))
	{
		ReparseKernelSourceText();
	}
}

void UComputeKernelFromText::ReparseKernelSourceText()
{
	if (SourceFile.FilePath.IsEmpty())
	{
		EntryPointName = FString();
		KernelSourceText = FString();
		PermutationSet = FComputeKernelPermutationSet();
		DefinitionsSet = FComputeKernelDefinitionsSet();
		InputParams.Empty();

		return;
	}

	FString FullKernelPath = FPaths::ConvertRelativePathToFull(SourceFile.FilePath);

	IPlatformFile& PlatformFileSystem = IPlatformFile::GetPlatformPhysical();
	if (!PlatformFileSystem.FileExists(*FullKernelPath))
	{
		UE_LOG(LogComputeFramework, Error, TEXT("Unable to find kernel file \"%s\""), *FullKernelPath);

		SourceFile = PrevSourceFile;
		return;
	}

	if (!FFileHelper::LoadFileToString(KernelSourceText, &PlatformFileSystem, *FullKernelPath))
	{
		UE_LOG(LogComputeFramework, Error, TEXT("Unable to read kernel file \"%s\""), *FullKernelPath);

		SourceFile = PrevSourceFile;
		return;
	}

	const FRegexPattern KernelEntryPointPattern(TEXT(R"(KERNEL_ENTRY_POINT\(\s*([a-zA-Z_]\w*)\s*\))"));
	{
		FRegexMatcher Matcher(KernelEntryPointPattern, KernelSourceText);
		if (Matcher.FindNext())
		{
			EntryPointName = Matcher.GetCaptureGroup(1);
		}
	}

	const FRegexPattern KernelPermutationBoolPattern(TEXT(R"(KERNEL_PERMUTATION_BOOL\(\s*([a-zA-Z_]\w*)\s*\))"));
	{
		FComputeKernelPermutationSet NewPermutationSet;

		FRegexMatcher Matcher(KernelPermutationBoolPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			NewPermutationSet.BooleanOptions.Emplace(Matcher.GetCaptureGroup(1));
		}

		PermutationSet = MoveTemp(NewPermutationSet);
	}

	const FRegexPattern KernelDefinePattern(TEXT(R"(KERNEL_DEFINE\(\s*([a-zA-Z_][\w]*)\s*\))"));
	{
		FComputeKernelDefinitionsSet NewDefinitionsSet;

		FRegexMatcher Matcher(KernelDefinePattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			NewDefinitionsSet.Defines.Emplace(Matcher.GetCaptureGroup(1));
		}

		DefinitionsSet = MoveTemp(NewDefinitionsSet);
	}

	const FRegexPattern KernelInputParamPattern(TEXT(R"(KERNEL_PARAM\(\s*(bool|int|uint|float)((?:[1-4])|(?:[1-4]x[1-4]))?\s*,\s*([a-zA-Z_]\w*)\s*\))"));
	{
		TArray<FShaderParamTypeDefinition> Params;

		FRegexMatcher Matcher(KernelInputParamPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			FShaderParamTypeDefinition Param = {};

			FString FundamentalType = Matcher.GetCaptureGroup(1);
			FString DimensionType	= Matcher.GetCaptureGroup(2);
			FString ParamName		= Matcher.GetCaptureGroup(3);

			Param.Name				= MoveTemp(ParamName);
			Param.FundamentalType	= FShaderParamTypeDefinition::ParseFundamental(FundamentalType);
			Param.DimType			= FShaderParamTypeDefinition::ParseDimension(DimensionType);
			Param.BindingType		= EShaderParamBindingType::ConstantParameter;

			switch (Param.DimType)
			{
			case EShaderFundamentalDimensionType::Scalar:
				Param.VectorDimension = 0;
				break;

			case EShaderFundamentalDimensionType::Vector:
				Param.VectorDimension = FShaderParamTypeDefinition::ParseVectorDimension(DimensionType);
				break;

			case EShaderFundamentalDimensionType::Matrix:
				FIntVector2 MtxDim = FShaderParamTypeDefinition::ParseMatrixDimension(DimensionType);
				Param.MatrixRowCount = MtxDim.X;
				Param.MatrixColumnCount = MtxDim.Y;
				break;
			}

			Param.ResetTypeDeclaration();
			Params.Emplace(MoveTemp(Param));
		}

		InputParams = MoveTemp(Params);
	}

	const FRegexPattern KernelReadExternPattern(TEXT(R"(KERNEL_EXTERN_READ\(\s*([a-zA-Z_]\w*)((\s*,\s*(?:bool|int|uint|float)(?:(?:[1-4]x[1-4])|(?:[1-4])|))+)\s*\))"));
	{
		TArray<FShaderFunctionDefinition> ExternalReadFunctions;

		FRegexMatcher Matcher(KernelReadExternPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			FShaderFunctionDefinition FunctionDesc;
			FunctionDesc.Name = Matcher.GetCaptureGroup(1);
			FunctionDesc.bHasReturnType = true;

			FString AllParameters = Matcher.GetCaptureGroup(2);
			const FRegexPattern ParameterPattern(TEXT(R"((bool|int|uint|float|half)((?:[1-4]x[1-4])|(?:[1-4])|))"));
			FRegexMatcher ParameterMatcher(ParameterPattern, *AllParameters);
			
			while (ParameterMatcher.FindNext())
			{
				FShaderParamTypeDefinition Param = {};

				FString FundamentalType = ParameterMatcher.GetCaptureGroup(1);
				FString DimensionType = ParameterMatcher.GetCaptureGroup(2);

				Param.FundamentalType = FShaderParamTypeDefinition::ParseFundamental(FundamentalType);
				Param.DimType = FShaderParamTypeDefinition::ParseDimension(DimensionType);

				switch (Param.DimType)
				{
				case EShaderFundamentalDimensionType::Scalar:
					Param.VectorDimension = 0;
					break;

				case EShaderFundamentalDimensionType::Vector:
					Param.VectorDimension = FShaderParamTypeDefinition::ParseVectorDimension(DimensionType);
					break;

				case EShaderFundamentalDimensionType::Matrix:
					FIntVector2 MtxDim = FShaderParamTypeDefinition::ParseMatrixDimension(DimensionType);
					Param.MatrixRowCount = MtxDim.X;
					Param.MatrixColumnCount = MtxDim.Y;
					break;
				}

				Param.ResetTypeDeclaration();
				FunctionDesc.ParamTypes.Emplace(MoveTemp(Param));
			}

			ExternalReadFunctions.Emplace(MoveTemp(FunctionDesc));
		}

		ExternalInputs = MoveTemp(ExternalReadFunctions);
	}

	const FRegexPattern KernelWriteExternPattern(TEXT(R"(KERNEL_EXTERN_WRITE\(\s*([a-zA-Z_]\w*)((\s*,\s*(?:bool|int|uint|float)(?:(?:[1-4]x[1-4])|(?:[1-4])|))+)\s*\))"));
	{
		TArray<FShaderFunctionDefinition> ExternalWriteFunctions;

		FRegexMatcher Matcher(KernelWriteExternPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			FShaderFunctionDefinition FunctionDesc;
			FunctionDesc.Name = Matcher.GetCaptureGroup(1);
			FunctionDesc.bHasReturnType = false;

			FString AllParameters = Matcher.GetCaptureGroup(2);
			const FRegexPattern ParameterPattern(TEXT(R"((bool|int|uint|float|half)((?:[1-4]x[1-4])|(?:[1-4])|))"));
			FRegexMatcher ParameterMatcher(ParameterPattern, *AllParameters);

			while (ParameterMatcher.FindNext())
			{
				FShaderParamTypeDefinition Param = {};

				FString FundamentalType = ParameterMatcher.GetCaptureGroup(1);
				FString DimensionType = ParameterMatcher.GetCaptureGroup(2);

				Param.FundamentalType = FShaderParamTypeDefinition::ParseFundamental(FundamentalType);
				Param.DimType = FShaderParamTypeDefinition::ParseDimension(DimensionType);

				switch (Param.DimType)
				{
				case EShaderFundamentalDimensionType::Scalar:
					Param.VectorDimension = 0;
					break;

				case EShaderFundamentalDimensionType::Vector:
					Param.VectorDimension = FShaderParamTypeDefinition::ParseVectorDimension(DimensionType);
					break;

				case EShaderFundamentalDimensionType::Matrix:
					FIntVector2 MtxDim = FShaderParamTypeDefinition::ParseMatrixDimension(DimensionType);
					Param.MatrixRowCount = MtxDim.X;
					Param.MatrixColumnCount = MtxDim.Y;
					break;
				}

				Param.ResetTypeDeclaration();
				FunctionDesc.ParamTypes.Emplace(MoveTemp(Param));
			}

			ExternalWriteFunctions.Emplace(MoveTemp(FunctionDesc));
		}

		ExternalOutputs = MoveTemp(ExternalWriteFunctions);
	}

	uint64 NewHash = 0;

	NewHash = FCrc::TypeCrc32(UniqueId, NewHash);
	NewHash = FCrc::TypeCrc32(*SourceFile.FilePath, NewHash);
	NewHash = FCrc::TypeCrc32(*KernelSourceText, NewHash);

	if (SourceHash != NewHash)
	{
		SourceHash = NewHash;
		// todo[CF]: Nofify graphs for recompilation
	}
}

#endif
