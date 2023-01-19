// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeORT.h"
#include "NNERuntimeORTModel.h"
#include "NNERuntimeORTUtils.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNXModelOptimizerInterface.h"
#include "NNECoreAttributeMap.h"
#include "NNECoreModelData.h"
#include "NNEProfilingTimer.h"
#include "RedirectCoutAndCerrToUeLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NNERuntimeORT)

using namespace NNX;

FGuid UNNERuntimeORTCpuImpl::GUID = FGuid((int32)'O', (int32)'N', (int32)'N', (int32)'X');
int32 UNNERuntimeORTCpuImpl::Version = 0x00000001;

bool UNNERuntimeORTCpuImpl::CanCreateModelData(FString FileType, TConstArrayView<uint8> FileData) const
{
	return FileType.Compare("onnx", ESearchCase::IgnoreCase) == 0;
}

TArray<uint8> UNNERuntimeORTCpuImpl::CreateModelData(FString FileType, TConstArrayView<uint8> FileData)
{
	if (!CanCreateModelData(FileType, FileData))
	{
		return {};
	}

	TUniquePtr<IModelOptimizer> Optimizer = UE::NNEUtils::Internal::CreateONNXToONNXModelOptimizer();

	FNNIModelRaw InputModel;
	InputModel.Data = FileData;
	InputModel.Format = ENNXInferenceFormat::ONNX;
	FNNIModelRaw OutputModel;
	FOptimizerOptionsMap Options;
	if (!Optimizer->Optimize(InputModel, OutputModel, Options))
	{
		return {};
	}

	int32 GuidSize = sizeof(UNNERuntimeORTCpuImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeORTCpuImpl::Version);
	TArray<uint8> Result;
	FMemoryWriter Writer(Result);
	Writer << UNNERuntimeORTCpuImpl::GUID;
	Writer << UNNERuntimeORTCpuImpl::Version;
	Writer.Serialize(OutputModel.Data.GetData(), OutputModel.Data.Num());
	return Result;
}

bool UNNERuntimeORTCpuImpl::CanCreateModelCPU(TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	int32 GuidSize = sizeof(UNNERuntimeORTCpuImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeORTCpuImpl::Version);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());

	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(UNNERuntimeORTCpuImpl::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(UNNERuntimeORTCpuImpl::Version), VersionSize) == 0;
	return bResult;
}

void UNNERuntimeORTCpuImpl::Init()
{
	check(!ORTEnvironment.IsValid());
	ORTEnvironment = MakeUnique<Ort::Env>();
}

TUniquePtr<UE::NNECore::IModelCPU> UNNERuntimeORTCpuImpl::CreateModelCPU(TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);
	check(ORTEnvironment.IsValid());

	if (!CanCreateModelCPU(ModelData))
	{
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}

	const UE::NNERuntimeORT::Private::FRuntimeConf InConf;
	UE::NNERuntimeORT::Private::FModelORTCpu* Model = new UE::NNERuntimeORT::Private::FModelORTCpu(ORTEnvironment.Get(), InConf);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());

	if (!Model->Init(Data))
	{
		delete Model;
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}
	UE::NNECore::IModelCPU* IModel = static_cast<UE::NNECore::IModelCPU*>(Model);
	return TUniquePtr<UE::NNECore::IModelCPU>(IModel);
}

#if PLATFORM_WINDOWS
bool UNNERuntimeORTDmlImpl::CanCreateModelCPU(TObjectPtr<UNNEModelData> ModelData) const
{
	check(ModelData != nullptr);

	int32 GuidSize = sizeof(UNNERuntimeORTCpuImpl::GUID);
	int32 VersionSize = sizeof(UNNERuntimeORTCpuImpl::Version);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());

	if (Data.Num() <= GuidSize + VersionSize)
	{
		return false;
	}

	bool bResult = FGenericPlatformMemory::Memcmp(&(Data[0]), &(UNNERuntimeORTCpuImpl::GUID), GuidSize) == 0;
	bResult &= FGenericPlatformMemory::Memcmp(&(Data[GuidSize]), &(UNNERuntimeORTCpuImpl::Version), VersionSize) == 0;
	return bResult;
}

TUniquePtr<UE::NNECore::IModelCPU> UNNERuntimeORTDmlImpl::CreateModelCPU(TObjectPtr<UNNEModelData> ModelData)
{
	check(ModelData != nullptr);
	check(ORTEnvironment.IsValid());

	if (!CanCreateModelCPU(ModelData))
	{
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}

	const UE::NNERuntimeORT::Private::FRuntimeConf InConf;
	UE::NNERuntimeORT::Private::FModelORTDml* Model = new UE::NNERuntimeORT::Private::FModelORTDml(ORTEnvironment.Get(), InConf);
	TConstArrayView<uint8> Data = ModelData->GetModelData(GetRuntimeName());

	if (!Model->Init(Data))
	{
		delete Model;
		return TUniquePtr<UE::NNECore::IModelCPU>();
	}
	UE::NNECore::IModelCPU* IModel = static_cast<UE::NNECore::IModelCPU*>(Model);
	return TUniquePtr<UE::NNECore::IModelCPU>(IModel);
}

#else // PLATFORM_WINDOWS

bool UNNERuntimeORTDmlImpl::CanCreateModelCPU(TObjectPtr<UNNEModelData> ModelData) const
{
	return false;
}

TUniquePtr<UE::NNECore::IModelCPU> UNNERuntimeORTDmlImpl::CreateModelCPU(TObjectPtr<UNNEModelData> ModelData)
{
	return TUniquePtr<UE::NNECore::IModelCPU>();
}

#endif // PLATFORM_WINDOWS