// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGHlsl.h"
#include "NNECoreAttributeMap.h"
#include "NNECoreModelData.h"
#include "NNERuntimeRDG.h"
#include "NNERuntimeRDGHlslHelper.h"
#include "NNERuntimeRDGModelHlsl.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNXModelOptimizerInterface.h"
#include "Hlsl/NNERuntimeRDGConv.h"
#include "Hlsl/NNERuntimeRDGConv.h"
#include "Hlsl/NNERuntimeRDGConvTranspose.h"
#include "Hlsl/NNERuntimeRDGElementWiseBinary.h"
#include "Hlsl/NNERuntimeRDGElementWiseUnary.h"
#include "Hlsl/NNERuntimeRDGElementWiseVariadic.h"
#include "Hlsl/NNERuntimeRDGGemm.h"
#include "Hlsl/NNERuntimeRDGInstanceNormalization.h"
#include "Hlsl/NNERuntimeRDGPad.h"
#include "Hlsl/NNERuntimeRDGUpsample.h"
#include "Hlsl/NNERuntimeRDGMatMul.h"

using namespace UE::NNERuntimeRDG::Private::Hlsl;

FGuid UNNERuntimeRDGHlslImpl::GUID = FGuid((int32)'R', (int32)'D', (int32)'G', (int32)'H');
int32 UNNERuntimeRDGHlslImpl::Version = 0x00000001;

bool UNNERuntimeRDGHlslImpl::Init()
{
	FOperatorRegistryHlsl* registry = FOperatorRegistryHlsl::Get();
	check(registry != nullptr);

	RegisterElementWiseUnaryOperators(*registry);
	RegisterElementWiseBinaryOperators(*registry);
	RegisterElementWiseVariadicOperators(*registry);
	RegisterGemmOperator(*registry);
	RegisterConvOperator(*registry);
	RegisterConvTransposeOperator(*registry);
	RegisterMatMulOperator(*registry);
	RegisterInstanceNormalizationOperator(*registry);
	RegisterUpsampleOperator(*registry);
	RegisterPadOperator(*registry);

	return true;
}

bool UNNERuntimeRDGHlslImpl::CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const
{
	return FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0;
}

bool UNNERuntimeRDGHlslImpl::CanCreateModelRDG(TObjectPtr<UNNEModelData> ModelData) const
{
	int32 GuidSize = sizeof(GUID);
	int32 VersionSize = sizeof(Version);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());
	
	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}
	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(Version), VersionSize) == 0;
	return bResult;
};

TArray<uint8> UNNERuntimeRDGHlslImpl::CreateModelData(FString FileType, TConstArrayView<uint8> FileData)
{
	if (!CanCreateModelData(FileType, FileData))
	{
		return {};
	}

	TUniquePtr<NNX::IModelOptimizer> Optimizer = UE::NNEUtils::Internal::CreateONNXToNNEModelOptimizer();
	Optimizer->AddValidator(MakeShared<FModelValidatorHlsl>());

	FNNIModelRaw InputModel;
	InputModel.Data = FileData;
	InputModel.Format = ENNXInferenceFormat::ONNX;

	FNNIModelRaw OutputModel;
	if (!Optimizer->Optimize(InputModel, OutputModel, {}))
	{
		return {};
	}

	TArray<uint8> Result;
	FMemoryWriter Writer(Result);
	
	Writer << GUID;
	Writer << Version;
	Writer.Serialize(OutputModel.Data.GetData(), OutputModel.Data.Num());
	return Result;
};

TUniquePtr<UE::NNECore::IModelRDG> UNNERuntimeRDGHlslImpl::CreateModelRDG(TObjectPtr<UNNEModelData> ModelData)
{
	if (!CanCreateModelRDG(ModelData))
	{
		return TUniquePtr<UE::NNECore::IModelRDG>();
	}

	UE::NNERuntimeRDG::Private::Hlsl::FModel* Model = new UE::NNERuntimeRDG::Private::Hlsl::FModel();
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());
	
	if (!Model->Init(Data))
	{
		delete Model;
		return TUniquePtr<UE::NNECore::IModelRDG>();
	}
	return TUniquePtr<UE::NNECore::IModelRDG>(Model);
}
