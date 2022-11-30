// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelOptimizer.h"
#include "NNECoreAttributeMap.h"
#include "Hlsl/NNIRuntimeRDGConv.h"
#include "Hlsl/NNIRuntimeRDGElementWiseBinary.h"
#include "Hlsl/NNIRuntimeRDGElementWiseUnary.h"
#include "Hlsl/NNIRuntimeRDGElementWiseVariadic.h"
#include "Hlsl/NNIRuntimeRDGConv.h"
#include "Hlsl/NNIRuntimeRDGConvTranspose.h"
#include "Hlsl/NNIRuntimeRDGGemm.h"
#include "Hlsl/NNIRuntimeRDGMatMul.h"
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

	bool Init(TConstArrayView<uint8> ModelData);

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override;
	virtual int RunShapeInference() override;

private:

	FMLOperatorHlsl* OpCreate(const FString& Name, TConstArrayView<FTensorDesc> InputTensorDesc, TConstArrayView<FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes);

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
bool FMLInferenceModelHlsl::Init(TConstArrayView<uint8> ModelData)
{
	check(ModelData.Num() > 0);
	FMLRuntimeFormat	Format;

	if (!LoadModel(ModelData, Format))
	{
		return false;
	}

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		TArray<FTensorDesc> Inputs;
		TArray<FTensorDesc> Outputs;
		UE::NNECore::FAttributeMap AttributeMap;

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			Inputs.Emplace(AllSymbolicTensorDescs[InputTensorIndex]);
		}
		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			Outputs.Emplace(AllSymbolicTensorDescs[OutputTensorIndex]);
		}
		for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		FMLOperatorHlsl* Op = OpCreate(TypeName, Inputs, Outputs, AttributeMap);

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
FMLOperatorHlsl* FMLInferenceModelHlsl::OpCreate(const FString& OpName, TConstArrayView<FTensorDesc> InputTensorDescs, TConstArrayView<FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& AttributeMap)
{
	FMLOperatorRegistryHlsl::OperatorCreateFunc CreateFn = FMLOperatorRegistryHlsl::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNX, Warning, TEXT("Hlsl MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	FMLOperatorHlsl* Op = CreateFn();

	if (!Op->Initialize(InputTensorDescs, OutputTensorDescs, AttributeMap))
	{
		UE_LOG(LogNNX, Warning, TEXT("Hlsl engine: Error initializing operator:%s"), *OpName);
		delete Op;
		return nullptr;
	}

	return Op;
}

int FMLInferenceModelHlsl::RunShapeInference()
{
	AllShapes.Empty();

	if (Operators.Num() == 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("No operators in model"));
		return -1;
	}

	static constexpr int32 MaxExpectedInput = 10;
	TArray<FTensorShape, TInlineAllocator<MaxExpectedInput>> InputShapes;
	TArray<FTensorShape> OutputShapes;
	TArray<bool> AllInitializedShapes;

	checkCode(AllInitializedShapes.SetNum(AllSymbolicTensorDescs.Num(), false););
	AllShapes.SetNum(AllSymbolicTensorDescs.Num());

	//Prime shape inference with model inputs
	for (int32 i = 0; i < InputTensorIndices.Num(); ++i)
	{
		const int32 InputTensorModelIndex = InputTensorIndices[i];

		AllShapes[InputTensorModelIndex] = InputTensorShapes[i];
		checkCode(AllInitializedShapes[InputTensorModelIndex] = true);
	}

	// Run shape inference on all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputShapes.Empty();
		OutputShapes.Empty();

		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			checkf(AllInitializedShapes[i] == true, TEXT("Input shape %d for operator %d should have been initialized."), i, Idx);
			InputShapes.Emplace(AllShapes[i]);
		}

		//If operator output shapes are not variable we don't need to run shape inference
		bool bShouldRunShapeInferenceForOperator = false;
		for (int32 i = 0; i < OperatorOutputTensorIndices[Idx].Num(); ++i)
		{
			const int32 OutputTensorIndex = OperatorOutputTensorIndices[Idx][i];

			if (!AllSymbolicTensorDescs[OutputTensorIndex].IsConcrete())
			{
				bShouldRunShapeInferenceForOperator = true;
				continue;
			}
			OutputShapes.Emplace(FTensorShape::MakeFromSymbolic(AllSymbolicTensorDescs[OutputTensorIndex].GetShape()));
		}

		//Otherwise we need to run shape inference for the operator
		if (bShouldRunShapeInferenceForOperator)
		{
			OutputShapes.Empty();

			const FMLOperatorHlsl* Op = Operators[Idx];

			if (Op->ComputeOutputShape(InputShapes, OutputShapes) != 0)
			{
				//Operator could not compute output shapes, meaning we can't allocate
				//output buffer before running the model. This engine does not support this.
				UE_LOG(LogNNX, Warning, TEXT("Could not deduce tensor shapes for this model during shape inference, HLSL engine wont support the model as it need to precompute all shapes for performance reasons."));
				AllShapes.Empty();
				OutputTensorShapes.Empty();
				return -1;
			}
		}

		for (int32 i = 0; i < OperatorOutputTensorIndices[Idx].Num(); ++i)
		{
			const int32 OutputTensorIndex = OperatorOutputTensorIndices[Idx][i];

			AllShapes[OutputTensorIndex] = OutputShapes[i];
			checkCode(AllInitializedShapes[OutputTensorIndex] = true);
		}
	}

	checkCode(
		for (int i = 0; i < AllInitializedShapes.Num(); ++i)
		{
			checkf(AllInitializedShapes[i], TEXT("Tensor at index %d, was not initialized by shape inference."));
		};
	);

	check(AllShapes.Num() == AllSymbolicTensorDescs.Num());

	return 0;
}

//
//
//
void FMLInferenceModelHlsl::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder)
{
	check(AllTensorRDGs.Num() == AllShapes.Num());

	static constexpr int32 MaxExpectedInput = 10;
	TArray<FTensorRDG, TInlineAllocator<MaxExpectedInput>> InputTensors;

	static constexpr int32 MaxExpectedOutput = 2;
	TArray<FTensorRDG, TInlineAllocator<MaxExpectedOutput>> OutputTensors;

	// Add passes for all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputTensors.Empty();
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			InputTensors.Emplace(AllTensorRDGs[i]);
		}
		OutputTensors.Empty();
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputTensors.Emplace(AllTensorRDGs[i]);
		}

		FMLOperatorHlsl* Op = Operators[Idx];

		Op->Dispatch(GraphBuilder, InputTensors, OutputTensors);
	}
}

//
//
//
class FMLRuntimeHlsl : public FMLRuntimeRDG
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

		UE::NNIRuntimeRDG::Private::Hlsl::RegisterElementWiseUnaryOperators(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterElementWiseBinaryOperators(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterElementWiseVariadicOperators(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterGemmOperator(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterConvOperator(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterConvTransposeOperator(*registry);
		UE::NNIRuntimeRDG::Private::Hlsl::RegisterMatMulOperator(*registry);

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

	virtual TUniquePtr<FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData) override
	{
		if (!CanCreateModel(ModelData))
		{
			return TUniquePtr<FMLInferenceModel>();
		}

		// Create the model and initialize it with the data not including the header
		FMLInferenceModelHlsl* Model = new FMLInferenceModelHlsl();
		if (!Model->Init(ModelData))
		{
			delete Model;
			return TUniquePtr<FMLInferenceModel>();
		}
		return TUniquePtr<FMLInferenceModel>(Model);
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
