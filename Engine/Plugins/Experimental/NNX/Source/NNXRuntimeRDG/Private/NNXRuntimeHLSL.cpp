// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXElementWiseBinaryCS.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNXRuntimeHLSLElementWiseBinaryOps.h"
#include "NNXRuntimeHLSLElementWiseUnaryOps.h"
#include "NNXRuntimeHLSLElementWiseVariadicOps.h"
#include "NNXRuntimeHLSLGemmOp.h"
#include "NNXRuntimeHLSLConvTransposeOp.h"
#include "NNXRuntimeHLSLMatMulOp.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNXRuntimeRDG.h"

namespace NNX
{

#define NNX_RUNTIME_HLSL_NAME TEXT("NNXRuntimeHlsl")

//
//
//
class FMLInferenceModelHlsl : public FMLInferenceModelRDG
{
public:

	FMLInferenceModelHlsl();
	~FMLInferenceModelHlsl();

	bool Init(UMLInferenceModel* InModel);

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override;

private:

	FMLOperatorHlsl* OpCreate(const FString& Name, TArrayView<const FMLTensorDesc> InputTensorDesc, TArrayView<const FMLTensorDesc> OutputTensorDescs, const FMLAttributeMap& Attributes);

	TArray<FMLOperatorHlsl*>	Operators;
};


//
//
//
FMLInferenceModelHlsl::FMLInferenceModelHlsl()
{
}

//
//
//
FMLInferenceModelHlsl::~FMLInferenceModelHlsl()
{
	// TODO: Release all the operators
}

//
//
//
bool FMLInferenceModelHlsl::Init(UMLInferenceModel* InModel)
{
	FMLRuntimeFormat	Format;

	if (!LoadModel(InModel, Format))
	{
		return false;
	}

	if (Format.Operators.Num() > 1)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create inference model, currently on single layer models are supported"));
		return false;
	}

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		// HACK: This works only for single layer networks
		TArray<FMLTensorDesc> OpInputTensors = InputTensors;
		TArray<FMLTensorDesc> OpOutputTensors = OutputTensors;

		// Attributes
		FMLAttributeMap AttributeMap;
		for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		FMLOperatorHlsl* Op = OpCreate(TypeName, OpInputTensors, OpOutputTensors, AttributeMap);

		if (!Op) //Op.Shader.IsNull())
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to create operator:%s"), *TypeName);

			// TODO: Cleanup operators
			return false;
		}

		Operators.Add(Op);
	}

	return true;
}

//
//
//
FMLOperatorHlsl* FMLInferenceModelHlsl::OpCreate(const FString& OpName, TArrayView<const FMLTensorDesc> InputTensorDescs, TArrayView<const FMLTensorDesc> OutputTensorDescs, const FMLAttributeMap& AttributeMap)
{
	FMLOperatorRegistryHlsl::MLOperatorCreateFunc CreateFn = FMLOperatorRegistryHlsl::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	FMLOperatorHlsl* Op = CreateFn();

	Op->Initialize(InputTensorDescs, OutputTensorDescs, AttributeMap);

	return Op;
}

//
//
//
void FMLInferenceModelHlsl::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings)
{
	// Add passes for all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		FMLOperatorHlsl* Op = Operators[Idx];

		Op->Dispatch(GraphBuilder, InInputBindings, OutOutputBindings);
	}
}

//
//
//
class FMLRuntimeHlsl : public IRuntime
{
public:

	FMLRuntimeHlsl() = default;

	virtual ~FMLRuntimeHlsl()
	{
	}

	bool Init()
	{
		FMLOperatorRegistryHlsl* registry = FMLOperatorRegistryHlsl::Get();
		check(registry != nullptr);

		RegisterElementWiseUnaryOperators(*registry);
		RegisterElementWiseBinaryOperators(*registry);
		RegisterElementWiseVariadicOperators(*registry);
		RegisterGemmOperator(*registry);
		RegisterConvTransposeOperator(*registry);
		RegisterMatMulOperator(*registry);

		return true;
	}

	virtual FString GetRuntimeName() const override
	{
		return NNX_RUNTIME_HLSL_NAME;
	}

	// Returns flags from ERuntimeSupportFlags
	virtual EMLRuntimeSupportFlags GetSupportFlags() const override
	{
		return EMLRuntimeSupportFlags::RDG;
	}

	virtual FMLInferenceModel* CreateInferenceModel(UMLInferenceModel* Model)
	{
		auto HlslModel = new FMLInferenceModelHlsl();

		if (!HlslModel->Init(Model))
		{
			delete HlslModel;
			HlslModel = nullptr;
		}

		return HlslModel;
	}
};



/** Globally accessible runtime */
static TUniquePtr<FMLRuntimeHlsl> GHlslRuntime;

//
//
//
static TUniquePtr<FMLRuntimeHlsl> FMLRuntimeHlslCreate()
{
	auto Runtime = MakeUnique<FMLRuntimeHlsl>();

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

} // NNX
