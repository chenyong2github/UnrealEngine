// Copyright Epic Games, Inc. All Rights Reserved.

#include "Hlsl/NNIRuntimeRDGConv.h"
#include "Hlsl/NNIRuntimeRDGConv.h"
#include "Hlsl/NNIRuntimeRDGConvTranspose.h"
#include "Hlsl/NNIRuntimeRDGElementWiseBinary.h"
#include "Hlsl/NNIRuntimeRDGElementWiseUnary.h"
#include "Hlsl/NNIRuntimeRDGElementWiseVariadic.h"
#include "Hlsl/NNIRuntimeRDGGemm.h"
#include "Hlsl/NNIRuntimeRDGInstanceNormalization.h"
#include "Hlsl/NNIRuntimeRDGPad.h"
#include "Hlsl/NNIRuntimeRDGUpsample.h"
#include "Hlsl/NNIRuntimeRDGMatMul.h"
#include "NNECoreAttributeMap.h"
#include "NNERuntimeRDGModelHLSL.h"
#include "NNXInferenceModel.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNXRuntimeFormat.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNXRuntimeRDG.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{

#define NNX_RUNTIME_HLSL_NAME TEXT("NNXRuntimeHlsl")

//
//
//
class FMLRuntimeHlsl : public NNX::FMLRuntimeRDG
{
public:

	FMLRuntimeHlsl() = default;

	virtual ~FMLRuntimeHlsl()
	{
	}

	bool Init()
	{
		NNX::FMLOperatorRegistryHlsl* registry = NNX::FMLOperatorRegistryHlsl::Get();
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

	virtual FString GetRuntimeName() const override
	{
		return NNX_RUNTIME_HLSL_NAME;
	}

	// Returns flags from ERuntimeSupportFlags
	virtual NNX::EMLRuntimeSupportFlags GetSupportFlags() const override
	{
		return NNX::EMLRuntimeSupportFlags::RDG;
	}

	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) override
	{
		if (!CanCreateModelData(FileType, FileData))
		{
			return {};
		}

		TUniquePtr<NNX::IModelOptimizer> Optimizer = NNEUtils::Internal::CreateONNXToNNEModelOptimizer();
		Optimizer->AddValidator(MakeShared<NNX::FModelValidatorHlsl>());

		FNNIModelRaw InputModel;
		InputModel.Data = FileData;
		InputModel.Format = ENNXInferenceFormat::ONNX;

		FNNIModelRaw OutputModel;
		if (!Optimizer->Optimize(InputModel, OutputModel, {}))
		{
			return {};
		}

		return NNX::ConvertToModelData(OutputModel.Data);
	};

	virtual TUniquePtr<NNX::FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData) override
	{
		if (!CanCreateModel(ModelData))
		{
			return TUniquePtr<NNX::FMLInferenceModel>();
		}

		// Create the model and initialize it with the data not including the header
		FModel* Model = new FModel();
		if (!Model->Init(ModelData))
		{
			delete Model;
			return TUniquePtr<NNX::FMLInferenceModel>();
		}
		return TUniquePtr<NNX::FMLInferenceModel>(Model);
	}
};

} // namespace UE::NNIRuntimeRDG::Private::Hlsl

namespace NNX
{

/** Globally accessible runtime */
static TUniquePtr<UE::NNIRuntimeRDG::Private::Hlsl::FMLRuntimeHlsl> GHlslRuntime;

//
//
//
static TUniquePtr<UE::NNIRuntimeRDG::Private::Hlsl::FMLRuntimeHlsl> FMLRuntimeHlslCreate()
{
	auto Runtime = MakeUnique<UE::NNIRuntimeRDG::Private::Hlsl::FMLRuntimeHlsl>();

	if (!Runtime->Init())
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create NNX HLSL runtime"));
		Runtime.Release();
	}

	return Runtime;
}

//
// Called on RDG runtime startup
//
IRuntime* FMLRuntimeHlslStartup()
{
	if (!GHlslRuntime)
	{
		GHlslRuntime = FMLRuntimeHlslCreate();
	}

	return GHlslRuntime.Get();
}

//
// Called on RDG runtime shutdown
//
void FMLRuntimeHlslShutdown()
{
	if (GHlslRuntime)
	{
		GHlslRuntime.Release();
	}
}

} // namespace NNX
