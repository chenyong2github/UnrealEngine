// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeKernelFromText.h"
#include "ComputeFramework/ComputeKernel.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"

#include "Internationalization/Regex.h"

UComputeKernelFromText::UComputeKernelFromText()
{
	UniqueId = FGuid::NewGuid();
}

void UComputeKernelFromText::PostLoad()
{
	Super::PostLoad();
#if WITH_EDITOR
	ReparseKernelSourceText();
#endif
}

#if WITH_EDITOR
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
	ON_SCOPE_EXIT
	{
		//FriendlyName = FPaths::GetCleanFilename(SourceFile.FilePath);
		//Name = (FriendlyName.IsEmpty()) ? TEXT("Unset"_SV) : FriendlyName;
	};

	if (SourceFile.FilePath.IsEmpty())
	{
		EntryPointName = FString();
		KernelSourceText = FString();
		PermutationSet = FComputeKernelPermutationSet();
		DefinitionsSet = FComputeKernelDefinitionsSet();
		InputParams.Empty();
		InputSRVs.Empty();
		Outputs.Empty();

		return;
	}

	FString FullKernelPath = FPaths::ConvertRelativePathToFull(SourceFile.FilePath);

	IPlatformFile& PlatformFileSystem = IPlatformFile::GetPlatformPhysical();
	if (!PlatformFileSystem.FileExists(*FullKernelPath))
	{
		UE_LOG(ComputeKernel, Error, TEXT("Unable to find kernel file \"%s\""), *FullKernelPath);

		SourceFile = PrevSourceFile;
		return;
	}

	if (!FFileHelper::LoadFileToString(KernelSourceText, &PlatformFileSystem, *FullKernelPath))
	{
		UE_LOG(ComputeKernel, Error, TEXT("Unable to read kernel file \"%s\""), *FullKernelPath);

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
				Param.MatrixColumnCount	= MtxDim.X;
				Param.MatrixRowCount	= MtxDim.Y;
				break;
			}

			Param.ResetTypeDeclaration();
			Params.Emplace(MoveTemp(Param));
		}

		InputParams = MoveTemp(Params);
	}

	const FRegexPattern KernelInputSRVPattern(TEXT(R"(KERNEL_SRV\(\s*(ByteAddress|Structured|Buffer|Texture1D|Texture2D|Texture3D|TextureCube)\s*<\s*(?:SNORM|UNORM)?\s*(int|uint|float)((?:[1-4])|(?:[1-4]x[1-4]))?\s*>\s*,\s*([a-zA-Z_]\w*)\s*\))"));
	{
		TArray<FShaderParamTypeDefinition> SRVs;

		FRegexMatcher Matcher(KernelInputSRVPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			FShaderParamTypeDefinition Param = {};

			FString ResourceType	= Matcher.GetCaptureGroup(1);
			FString FundamentalType = Matcher.GetCaptureGroup(2);
			FString DimensionType	= Matcher.GetCaptureGroup(3);
			FString ParamName		= Matcher.GetCaptureGroup(4);

			Param.Name				= MoveTemp(ParamName);
			Param.FundamentalType	= FShaderParamTypeDefinition::ParseFundamental(FundamentalType);
			Param.DimType			= FShaderParamTypeDefinition::ParseDimension(DimensionType);
			Param.BindingType		= EShaderParamBindingType::ReadOnlyResource;
			Param.ResourceType		= FShaderParamTypeDefinition::ParseResource(ResourceType);

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
				Param.MatrixColumnCount = MtxDim.X;
				Param.MatrixRowCount	= MtxDim.Y;
				break;
			}

			Param.ResetTypeDeclaration();
			SRVs.Emplace(MoveTemp(Param));
		}

		InputSRVs = MoveTemp(SRVs);
	}

	const FRegexPattern KernelOutputPattern(TEXT(R"(KERNEL_UAV\(\s*RW(ByteAddress|Structured|Buffer|Texture1D|Texture2D|Texture3D|TextureCube)\s*<\s*(?:SNORM|UNORM)?\s*(int|uint|float)((?:[1-4])|(?:[1-4]x[1-4]))?\s*>\s*,\s*([a-zA-Z_]\w*)\s*\))"));
	{
		TArray<FShaderParamTypeDefinition> OutputParams;

		FRegexMatcher Matcher(KernelOutputPattern, KernelSourceText);
		while (Matcher.FindNext())
		{
			FShaderParamTypeDefinition Param = {};

			FString ResourceType	= Matcher.GetCaptureGroup(1);
			FString FundamentalType = Matcher.GetCaptureGroup(2);
			FString DimensionType	= Matcher.GetCaptureGroup(3);
			FString ParamName		= Matcher.GetCaptureGroup(4);

			Param.Name				= MoveTemp(ParamName);
			Param.FundamentalType	= FShaderParamTypeDefinition::ParseFundamental(FundamentalType);
			Param.DimType			= FShaderParamTypeDefinition::ParseDimension(DimensionType);
			Param.BindingType		= EShaderParamBindingType::ReadWriteResource;
			Param.ResourceType		= FShaderParamTypeDefinition::ParseResource(ResourceType);

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
				Param.MatrixColumnCount = MtxDim.X;
				Param.MatrixRowCount	= MtxDim.Y;
				break;
			}

			Param.ResetTypeDeclaration();
			OutputParams.Emplace(MoveTemp(Param));
		}

		Outputs = MoveTemp(OutputParams);
	}

	/*
	uint64 NewHash = 0;

	NewHash = Hash(NewHash, UniqueId);
	NewHash = Hash(NewHash, *SourceFile.FilePath);
	NewHash = Hash(NewHash, *KernelSource);

	if (SourceHash != NewHash)
	{
		SourceHash = NewHash;
		// Trigger recompile
	}
	*/
}

#endif
