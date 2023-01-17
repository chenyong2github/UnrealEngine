// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeCPU.h"
#include "NNERuntimeCPUModel.h"
#include "NNERuntimeCPUUtils.h"
#include "NNXModelOptimizer.h"
#include "NNXModelOptimizerInterface.h"
#include "NNECoreAttributeMap.h"
#include "NNECoreModelData.h"
#include "NNEProfilingTimer.h"
#include "RedirectCoutAndCerrToUeLog.h"

using namespace NNX;

FGuid UNNERuntimeCPUImpl::GUID = FGuid((int32)'R', (int32)'C', (int32)'P', (int32)'U');
int32 UNNERuntimeCPUImpl::Version = 0x00000001;

bool UNNERuntimeCPUImpl::CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const
{
	return FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0;
}

TArray<uint8> UNNERuntimeCPUImpl::CreateModelData(FString FileType, TConstArrayView<uint8> FileData)
{
	if (!CanCreateModelData(FileType, FileData))
	{
		return {};
	}

	TUniquePtr<IModelOptimizer> Optimizer = CreateONNXToONNXModelOptimizer();

	FNNIModelRaw InputModel;
	InputModel.Data = FileData;
	InputModel.Format = ENNXInferenceFormat::ONNX;
	FNNIModelRaw OutputModel;
	FOptimizerOptionsMap Options;
	if (!Optimizer->Optimize(InputModel, OutputModel, Options))
	{
		return {};
	}

	int32 GuidSize = sizeof(UNNERuntimeCPUImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeCPUImpl::Version);
	TArray<uint8> Result;
	FMemoryWriter Writer(Result);
	Writer << UNNERuntimeCPUImpl::GUID;
	Writer << UNNERuntimeCPUImpl::Version;
	Writer.Serialize(OutputModel.Data.GetData(), OutputModel.Data.Num());
	return Result;
}

bool UNNERuntimeCPUImpl::CanCreateModelCPU(TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);
	
	int32 GuidSize = sizeof(UNNERuntimeCPUImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeCPUImpl::Version);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());
	
	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}
	
	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(UNNERuntimeCPUImpl::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(UNNERuntimeCPUImpl::Version), VersionSize) == 0;
	return bResult;
}

TUniquePtr<UE::NNECore::IModelCPU> UNNERuntimeCPUImpl::CreateModelCPU(TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);
	
	if (!CanCreateModelCPU(ModelData))
	{
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}

	const UE::NNERuntimeCPU::Private::FRuntimeConf InConf;
	UE::NNERuntimeCPU::Private::FModelCPU* Model = new UE::NNERuntimeCPU::Private::FModelCPU(&NNEEnvironmentCPU, InConf);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());
	
	if (!Model->Init(Data))
	{
		delete Model;
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}
	UE::NNECore::IModelCPU* IModel = static_cast<UE::NNECore::IModelCPU*>(Model);
	return TUniquePtr<UE::NNECore::IModelCPU>(IModel);
}