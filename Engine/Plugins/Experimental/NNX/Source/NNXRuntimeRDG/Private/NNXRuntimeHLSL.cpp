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

	bool Init(UMLInferenceModel* InModel);

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override;

private:

	FMLOperatorHlsl* OpCreate(const FString& Name, TArrayView<const FTensor> InputTensorDesc, TArrayView<const FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes);

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
	check(InModel != nullptr);
	FMLRuntimeFormat	Format;

	if (!LoadModel(InModel->GetFormatDesc(), Format))
	{
		return false;
	}

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		TArray<FTensor> OpInputTensors;
		TArray<FTensor> OpOutputTensors;
		UE::NNECore::FAttributeMap AttributeMap;

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			FTensorDesc SymbolicTensorDesc = AllSymbolicTensors[InputTensorIndex];
			//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
			OpInputTensors.Emplace(FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
		}
		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			FTensorDesc SymbolicTensorDesc = AllSymbolicTensors[OutputTensorIndex];
			//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
			OpOutputTensors.Emplace(FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
		}
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
FMLOperatorHlsl* FMLInferenceModelHlsl::OpCreate(const FString& OpName, TArrayView<const FTensor> InputTensorDescs, TArrayView<const FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& AttributeMap)
{
	FMLOperatorRegistryHlsl::OperatorCreateFunc CreateFn = FMLOperatorRegistryHlsl::Get()->OpFind(OpName);

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
void FMLInferenceModelHlsl::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder)
{
	check(AllTensorBindings.Num() == AllTensors.Num());
	checkCode(
		for (int32 i = 0; i < AllTensorBindings.Num(); ++i)
		{
			check(AllTensors[i].GetDataSize() == AllTensorBindings[i].SizeInBytes);
		}
	);

	static constexpr int32 MaxExpectedInput = 10;
	TArray<FMLTensorBinding, TInlineAllocator<MaxExpectedInput>> InputBindings;

	static constexpr int32 MaxExpectedOutput = 2;
	TArray<FMLTensorBinding, TInlineAllocator<MaxExpectedOutput>> OutputBindings;

	// Add passes for all operators
	for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
	{
		InputBindings.Empty();
		for (int32 i : OperatorInputTensorIndices[Idx])
		{
			InputBindings.Emplace(AllTensorBindings[i]);
		}
		OutputBindings.Empty();
		for (int32 i : OperatorOutputTensorIndices[Idx])
		{
			OutputBindings.Emplace(AllTensorBindings[i]);
		}

		FMLOperatorHlsl* Op = Operators[Idx];

		//TODO jira 169354 pass shape information to Op.Dispatch probably via an object containing both mem info and shape (from AllTensors).
		Op->Dispatch(GraphBuilder, InputBindings, OutputBindings);
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

	virtual TUniquePtr<IModelOptimizer> CreateModelOptimizer() const
	{
		TUniquePtr<IModelOptimizer> ModelOptimizer = CreateONNXToNNXModelOptimizer();
		check(ModelOptimizer.IsValid());
		
		ModelOptimizer->AddValidator(MakeShared<FModelValidatorHlsl>());
		
		return ModelOptimizer;
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
