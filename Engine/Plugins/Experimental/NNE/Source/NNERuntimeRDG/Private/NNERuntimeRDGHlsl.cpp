// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "NNECoreAttributeMap.h"
#include "NNERuntimeRDGModelHlsl.h"
#include "NNXInferenceModel.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNXRuntimeFormat.h"
#include "NNERuntimeRDGHlslHelper.h"
#include "NNERuntimeRDG.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{

#define NNX_RUNTIME_HLSL_NAME TEXT("NNXRuntimeHlsl")

//
//
//
class FRuntimeHlsl : public FRuntimeRDG
{
public:

	FRuntimeHlsl() = default;

	virtual ~FRuntimeHlsl()
	{
	}

	bool Init()
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
		Optimizer->AddValidator(MakeShared<FModelValidatorHlsl>());

		FNNIModelRaw InputModel;
		InputModel.Data = FileData;
		InputModel.Format = ENNXInferenceFormat::ONNX;

		FNNIModelRaw OutputModel;
		if (!Optimizer->Optimize(InputModel, OutputModel, {}))
		{
			return {};
		}

		return ConvertToModelData(OutputModel.Data);
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

/** Globally accessible runtime */
static TUniquePtr<NNERuntimeRDG::Private::Hlsl::FRuntimeHlsl> GHlslRuntime;

//
//
//
static TUniquePtr<NNERuntimeRDG::Private::Hlsl::FRuntimeHlsl> FRuntimeHlslCreate()
{
	auto Runtime = MakeUnique<NNERuntimeRDG::Private::Hlsl::FRuntimeHlsl>();

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
IRuntime* FRuntimeHlslStartup()
{
	if (!GHlslRuntime)
	{
		GHlslRuntime = FRuntimeHlslCreate();
	}

	return GHlslRuntime.Get();
}

//
// Called on RDG runtime shutdown
//
void FRuntimeHlslShutdown()
{
	if (GHlslRuntime)
	{
		GHlslRuntime.Release();
	}
}

} // namespace UE::NNERuntimeRDG::Private::Hlsl
