// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXModelOptimizer.h"
#include "NNXModelOptimizerInterface.h"
#include "NNXCore.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelBuilder.h"

#ifdef PLATFORM_NNX_MICROSOFT
// Prevent some of false-positives when doing static analysis
__pragma(warning(disable: 6385))
#endif

#if PLATFORM_WINDOWS
#define ORT_STRING_CAST TCHAR_TO_WCHAR
#else
#define ORT_STRING_CAST TCHAR_TO_ANSI
#endif

#include "NNXThirdPartyWarningDisabler.h"
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/ort_model_optimizer_api.h"
#include "core/session/onnxruntime_cxx_api.h"
NNX_THIRD_PARTY_INCLUDES_END


#define Print(Format, ...) UE_LOG(LogNNX, Display, Format, __VA_ARGS__)

class FModelGraphPrinter
{
public:

	FModelGraphPrinter(Ort::IModelGraph* InGraph)
		: Graph(InGraph)
	{
		UE_LOG(LogNNX, Display, TEXT("Visiting model:%s"), ANSI_TO_TCHAR(Graph->GetGraphInfo().name));

		Storage.SetNum(2048);
	}

	void Run()
	{
		auto GraphInfo = Graph->GetGraphInfo();	

		Print(TEXT("Graph:%s"), ANSI_TO_TCHAR(GraphInfo.name));
        Print(TEXT("- Inputs:%d"), GraphInfo.inputCount);
		for (int c = 0; c < GraphInfo.inputCount; ++c) 
		{
			VisitTensor(Graph->GetGraphInput(c));
		}

		Print(TEXT("- Outputs:%d"), GraphInfo.outputCount);
		for (int c = 0; c < GraphInfo.outputCount; ++c) 
		{
			VisitTensor(Graph->GetGraphOutput(c));
		}

		Print(TEXT("- Nodes:%d"), GraphInfo.nodeCount);
		for (int NodeIdx = 0; NodeIdx < GraphInfo.nodeCount; ++NodeIdx) 
		{
			VisitNode(Graph->GetNode(NodeIdx));
		}
        
		Print(TEXT("- Tensor initializers:%d"), GraphInfo.tensorInitializerCount);
	}

	void VisitNode(const Ort::GraphNode& Node)
	{
		Ort::GraphNodeInfo	NodeInfo = Graph->GetNodeInfo(Node);

		Print(TEXT("Node op:%s name:%s"), ANSI_TO_TCHAR(NodeInfo.opName), ANSI_TO_TCHAR(NodeInfo.name));

		Print(TEXT("- Attribs:%d"), NodeInfo.attributeCount);
		for (int AttrIdx = 0; AttrIdx < NodeInfo.attributeCount; ++AttrIdx)
		{
			VisitAttrib(Node, AttrIdx);
			//Visit(Graph->GetNodeAttributeValue(Node, AttrIdx));
		}

		Print(TEXT("- Inputs:%d"), NodeInfo.inputCount);
		for (int InIdx = 0; InIdx < NodeInfo.inputCount; ++InIdx)
		{
			VisitTensor(Graph->GetNodeInput(Node, InIdx));
		}

		Print(TEXT("- Outputs:%d"), NodeInfo.outputCount);
		for (int OutIdx = 0; OutIdx < NodeInfo.outputCount; ++OutIdx)
		{
			VisitTensor(Graph->GetNodeOutput(Node, OutIdx));
		}
	}

	void VisitAttrib(const Ort::GraphNode& Node, int AttrIdx)
	{
		Ort::GraphAttributeInfo Attrib = Graph->GetNodeAttribute(Node, AttrIdx);
		Ort::GraphAttributeValue Value = Graph->GetNodeAttributeValue(Node, AttrIdx);

		Print(TEXT("   %s %d"), ANSI_TO_TCHAR(Attrib.name), Attrib.type);
		
		if (Value.type == Ort::GraphAttributeValue::kFloat)
		{
            Print(TEXT("      %f"), Value.f);
		}
        else if (Value.type == Ort::GraphAttributeValue::kInt)
		{
			Print(TEXT("      %lld"), Value.i);
		}
		else if (Value.type == Ort::GraphAttributeValue::kString)
		{
			Print(TEXT("      %s"), Value.s);
		}
        else if (Value.type == Ort::GraphAttributeValue::kFloats)
		{
            for (int c = 0; c < Value.count; ++c)
			{
				Print(TEXT("      %f"), Value.floats[c]);
			}
		}
		else if (Value.type == Ort::GraphAttributeValue::kInts) 
		{
			for (int c = 0; c < Value.count; ++c)
			{
				Print(TEXT("      %lld"), Value.ints[c]);
			}
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Unsupported attribute value type"));
		}
	}

	void VisitTensor(const Ort::GraphTensorInfo& Tensor) 
	{
		FAnsiStringBuilderBase	Str;

		Str.Appendf("   %-50s ", Tensor.name);
		Str.Append(" [ ");

		for (int c = 0; c < Tensor.shapeLen; ++c) 
		{
			if (Tensor.shape[c] == 0)
				Str.Append("N");
			else
				Str.Appendf("%d", Tensor.shape[c]);

			Str.Appendf("%c", c < Tensor.shapeLen - 1 ? ',' : ' ');
		}
		
		Str.Append("]");
		Str.Appendf(" type:%s", Ort::GraphTensorDataTypeToString(Tensor.dataType));
		
		Ort::GraphTensorInitializer	TensorInit = Graph->GetTensorInitializer(Tensor.name);
		size_t						DataSize = 0;

		if (TensorInit) 
		{
			DataSize = Graph->GetTensorDataSize(TensorInit);
			if (DataSize > Storage.Num())
			{
				Storage.SetNum(DataSize);
			}

			Str.Appendf(" size:%d", (int) DataSize);
		}

		Str.AppendChar('\0');
		UE_LOG(LogNNX, Display, TEXT("%s"), ANSI_TO_TCHAR(Str.GetData()));
		Str.Reset();
		
		if (DataSize)
		{
			if (Graph->GetTensorData(TensorInit, Storage.GetData(), DataSize, 0) == 0) 
			{
				int NElems = 10;

				if (Tensor.shapeLen == 1) 
				{
					if (Tensor.shape[0] < NElems)
					{
                        NElems = Tensor.shape[0];
					}
				} 
				else 
				{
                    const int dim = Tensor.shape[Tensor.shapeLen - 1];

					if (dim < NElems)
					{
						NElems = dim;
					}
				}

				for (int ec = 0; ec < NElems; ++ec) 
				{
					if (Tensor.dataType == Ort::GraphTensorDataType::kFloat)
					{
						UE_LOG(LogNNX, Display, TEXT("      %f"), ((float*) Storage.GetData())[ec]);
					}
                    else if (Tensor.dataType == Ort::GraphTensorDataType::kInt32)
					{
						UE_LOG(LogNNX, Display, TEXT("      %d"), ((int32*) Storage.GetData())[ec]);
					}
					else if (Tensor.dataType == Ort::GraphTensorDataType::kUInt32)
					{
						UE_LOG(LogNNX, Display, TEXT("      %u"), ((uint32*) Storage.GetData())[ec]);
					}
                    else if (Tensor.dataType == Ort::GraphTensorDataType::kInt64)
					{
						UE_LOG(LogNNX, Display, TEXT("      %lld"), ((int64*)Storage.GetData())[ec]);
					}
					else if (Tensor.dataType == Ort::GraphTensorDataType::kUInt64)
					{
						UE_LOG(LogNNX, Display, TEXT("      %llu"), ((uint64*) Storage.GetData())[ec]);
					}
				}
			}
			else 
			{
                UE_LOG(LogNNX, Warning, TEXT("Failed to read tensor data :%s"), Tensor.name);
			}
		}
	}

private:
	TUniquePtr<Ort::IModelGraph>	Graph;
	TArray<uint8>					Storage;
};

#undef Print


#define CASE(GType, Type) case Ort::GraphTensorDataType::GType: return ENNETensorDataType::Type

ENNETensorDataType GetDataTypeFromGraphTensor(Ort::GraphTensorDataType TensorDataType)
{
	switch (TensorDataType)
	{
		CASE(kFloat, Float);
		CASE(kUInt8, UInt8);
		CASE(kInt8, Int8);
		CASE(kUInt16, UInt16);
		CASE(kInt16, Int16);
		CASE(kInt32, Int32);
		CASE(kInt64, Int64);
		//CASE(kString, String);
		CASE(kBool, Boolean);
		CASE(kFloat16, Half);
		CASE(kDouble, Double);
		CASE(kUInt32, UInt32);
		CASE(kUInt64, UInt64);
		CASE(kComplex64, Complex64);
		CASE(kComplex128, Complex128);
		CASE(kBFloat16, BFloat16);

		default:
			return ENNETensorDataType::None;
	}
}

#undef CASE

namespace NNX
{
class FModelOptimizerBase : public IModelOptimizer
{
protected:
	TArray<TSharedPtr<IModelOptimizerPass>> OptimizationPasses;
	TArray<TSharedPtr<IModelValidator>> Validators;

	virtual bool ValidateInputModel(const FNNIModelRaw& InputModel)
	{
		if (InputModel.Format != ENNXInferenceFormat::ONNX)
		{
			UE_LOG(LogNNX, Warning, TEXT("Optimizer %s is expecting ONNX input format."), *GetName());
			return false;
		}

		OrtStatusPtr Status = OrtValidateModelFromMemory(InputModel.Data.GetData(), InputModel.Data.Num());
		if (Status)
		{
			UE_LOG(LogNNX, Warning, TEXT("Input ONNX model is invalid: %s, Model won't be optimized"), ANSI_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status)));
			return false;
		}
		
		return true;
	}
	
public:
	virtual void AddOptimizationPass(TSharedPtr<IModelOptimizerPass> ModelOptimizerPass) override
	{
		if (ModelOptimizerPass.IsValid())
		{
			OptimizationPasses.Add(ModelOptimizerPass);
		}
	}

	virtual void AddValidator(TSharedPtr<IModelValidator> ModelValidator) override
	{
		if (ModelValidator.IsValid())
		{
			Validators.Add(ModelValidator);
		}
	}

	bool IsModelValid(const FNNIModelRaw& ModelToValidate, const FOptimizerOptionsMap& Options)
	{
		bool bIsModelValid = true;

		for (TSharedPtr<IModelValidator>& Validator : Validators)
		{
			check(Validator.IsValid());
			if (!Validator->ValidateModel(ModelToValidate, Options))
			{
				UE_LOG(LogNNX, Warning, TEXT("Model validator %s detected an error."), *(Validator->GetName()));
				bIsModelValid = false;
			}
		}
		return bIsModelValid;
	}

	bool ApplyAllPassesAndValidations(FNNIModelRaw& OptimizedModel, const FOptimizerOptionsMap& Options)
	{
		if (!IsModelValid(OptimizedModel, Options))
		{
			UE_LOG(LogNNX, Warning, TEXT("Model is not valid, skipping optimization passes."));
			return false;
		}
		
		for (TSharedPtr<IModelOptimizerPass>& Pass : OptimizationPasses)
		{
			check(Pass.IsValid());
			
			if (!Pass->ApplyPass(OptimizedModel, Options))
			{
				UE_LOG(LogNNX, Warning, TEXT("Error while executing model optimisation pass %s."), *(Pass->GetName()));
				return false;
			}
			if (!IsModelValid(OptimizedModel, Options))
			{
				UE_LOG(LogNNX, Warning, TEXT("Model validation failed after optimisation pass %s."), *(Pass->GetName()));
				return false;
			}
		}

		return true;
	}

	bool Optimize(const FNNIModelRaw& InputModel, FNNIModelRaw& OptimizedModel, const FOptimizerOptionsMap& Options) override
	{
		OptimizedModel = FNNIModelRaw{};
		
		if (!ValidateInputModel(InputModel))
		{
			return false;
		}

		OptimizedModel = InputModel;
		return ApplyAllPassesAndValidations(OptimizedModel, Options);
	}
};

class FONNXModelValidator : public IModelValidator
{
public:
	virtual FString GetName() const
	{
		return TEXT("ONNX Model validator");
	}

	virtual bool ValidateModel(const FNNIModelRaw& InputModel, const FOptimizerOptionsMap& Options) const override
	{
		FMLRuntimeFormat	Format;

		ENNXInferenceFormat FormatType = InputModel.Format;
		if (FormatType != ENNXInferenceFormat::ONNX)
		{
			UE_LOG(LogNNX, Warning, TEXT("Unsupported format type for validator %s"), *GetName());
			return false;
		}

		OrtStatusPtr Status = OrtValidateModelFromMemory(InputModel.Data.GetData(), InputModel.Data.Num());
		if (Status)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to validate ONNX model: %s"), ANSI_TO_TCHAR(Ort::GetApi().GetErrorMessage(Status)));
			return false;
		}

		return true;
	}
};

class FModelOptimizerONNXToONNX : public FModelOptimizerBase
{
public:
	virtual FString GetName() const override
	{
		return TEXT("NNXModelOptimizerFromONNXToONNX");
	}

	//TODO jira 167591: investigate if it is possible to optimize ONNX model and serialize it back as ONNX.
	//Among other benefits applying L1 could cut down on the model size. Atm this optimizer is a pass trough.
	//Bonus: Can we get a validator for onnx format?
};

static void OnOrtLog(const char* LogMsg)
{
	UE_LOG(LogNNX, Warning, TEXT("%s"), ANSI_TO_TCHAR(LogMsg));
}

class FModelOptimizerONNXToORT : public FModelOptimizerBase
{
public:
	virtual FString GetName() const override
	{
		return TEXT("NNXModelOptimizerONNXToORT");
	}

	virtual bool Optimize(const FNNIModelRaw& InputModel, FNNIModelRaw& OptimizedModel, const FOptimizerOptionsMap& Options) override
	{
		OptimizedModel = FNNIModelRaw{};
		
		if (!ValidateInputModel(InputModel))
		{
			return false;
		}

		TUniquePtr<Ort::IModelGraph> Graph = ConvertONNXToORTModelGraph(InputModel.Data);

		//TODO jira 167588: Serialize the Graph to a buffer and store it in OutModel.Data
		//allowing to initiate a session from ORT format for NN engine supporting this.
		//Allowing model optimization to be applyed at cooking time.
		//OptimizedModel = Graph.Serialize;
		//return ApplyAllPasses(OptimizedModel, Options);
		return false;
	}

protected:
	TUniquePtr<Ort::IModelGraph> ConvertONNXToORTModelGraph(TConstArrayView<uint8> ONNXData)
	{
		Ort::ModelOptimizeOptions	Opts{};

		Opts.logCallback = OnOrtLog;

		TUniquePtr<Ort::IModelGraph> Graph(OrtOptimizeModelFromMemory(ONNXData.GetData(), ONNXData.Num(), Opts));
		if (!Graph)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to load ONNX model from memory"));
		}

		//auto Printer = MakeUnique<FModelGraphPrinter>(Graph.Get());
		//Printer->Run();

		return Graph;
	}
};

class FModelOptimizerONNXToNNXRT : public FModelOptimizerONNXToORT
{
public:
	virtual FString GetName() const override
	{
		return TEXT("FNNXModelOptimizerONNXToNNX");
	}
	
	virtual bool Optimize(const FNNIModelRaw& InputModel, FNNIModelRaw& OptimizedModel, const FOptimizerOptionsMap& Options) override
	{
		OptimizedModel = FNNIModelRaw{};
		
		if (!ValidateInputModel(InputModel))
		{
			return false;
		}

		TUniquePtr<Ort::IModelGraph> Graph = ConvertONNXToORTModelGraph(InputModel.Data);
		
		if (!BuildNNXFormat(Graph.Get(), OptimizedModel.Data))
		{
			UE_LOG(LogNNX, Warning, TEXT("Error while building ORT ModelGraph."));
			return false;
		}

		OptimizedModel.Format = ENNXInferenceFormat::NNXRT;

		return ApplyAllPassesAndValidations(OptimizedModel, Options);
	}

private:
	bool BuildNNXFormat(Ort::IModelGraph* Graph, TArray<uint8>& NNXData)
	{
		TUniquePtr<NNX::IMLModelBuilder>	Builder(NNX::CreateNNXModelBuilder());

		Ort::GraphInfo GraphInfo = Graph->GetGraphInfo();

		Builder->Begin(GraphInfo.name);

		// Add tensors for graph inputs
		for (int Idx = 0; Idx < GraphInfo.inputCount; ++Idx)
		{
			Ort::GraphTensorInfo TensorInfo = Graph->GetGraphInput(Idx);

			ENNETensorDataType DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);

			auto Tensor = 
				Builder->AddTensor(
					ANSI_TO_TCHAR(TensorInfo.name),
					DataType,
					MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
				);

			Builder->AddInput(Tensor);
		}

		// Add tensors for graph outputs
		for (int Idx = 0; Idx < GraphInfo.outputCount; ++Idx)
		{
			Ort::GraphTensorInfo TensorInfo = Graph->GetGraphOutput(Idx);

			ENNETensorDataType DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);

			auto Tensor =
				Builder->AddTensor(
					ANSI_TO_TCHAR(TensorInfo.name),
					DataType,
					MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
				);

			Builder->AddOutput(Tensor);
		}

		// Traverse all the nodes get their inputs, outputs and tensor data
		TArray<uint8> TensorDataBlob;

		for (int Idx = 0; Idx < GraphInfo.nodeCount; ++Idx)
		{
			Ort::GraphNode		Node = Graph->GetNode(Idx);
			Ort::GraphNodeInfo	NodeInfo = Graph->GetNodeInfo(Node);

			auto Op = Builder->AddOperator(NodeInfo.opName);

			for (int InIdx = 0; InIdx < NodeInfo.attributeCount; ++InIdx)
			{
				Ort::GraphAttributeInfo AttrInfo = Graph->GetNodeAttribute(Node, InIdx);
				Ort::GraphAttributeValue AttrValue = Graph->GetNodeAttributeValue(Node, InIdx);

				if (AttrInfo.type == Ort::GraphAttributeType::kFloat)
				{
					Builder->AddOperatorAttribute(Op, AttrInfo.name, FNNEAttributeValue(AttrValue.f));
				}
				else if (AttrInfo.type == Ort::GraphAttributeType::kInt)
				{
					Builder->AddOperatorAttribute(Op, AttrInfo.name, FNNEAttributeValue((int32)AttrValue.i));
				}
				else if (AttrInfo.type == Ort::GraphAttributeType::kInts)
				{
					TArray<int32> Values;
					Values.SetNumUninitialized(AttrValue.count);

					for (int i = 0; i < AttrValue.count; i++)
					{
						Values[i] = (int32)AttrValue.ints[i];
					}

					Builder->AddOperatorAttribute(Op, AttrInfo.name, FNNEAttributeValue(Values));
				}
				else if (AttrInfo.type == Ort::GraphAttributeType::kString)
				{
					Builder->AddOperatorAttribute(Op, AttrInfo.name, FNNEAttributeValue(FString(AttrValue.s)));
				}
				else
				{
					//TODO: better error reporting add type (example: sparse tensor) and name of the actual node if any (not the op name but the node one)
					UE_LOG(LogNNX, Warning, TEXT("Unsupported attribute type for attribute '%s' in node '%s'"), ANSI_TO_TCHAR(AttrInfo.name), ANSI_TO_TCHAR(NodeInfo.opName));
				}
			}

			for (int InIdx = 0; InIdx < NodeInfo.inputCount; ++InIdx)
			{
				Ort::GraphTensorInfo		TensorInfo = Graph->GetNodeInput(Node, InIdx);
				Ort::GraphTensorInitializer	TensorInit = Graph->GetTensorInitializer(TensorInfo.name);
				ENNETensorDataType			DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);
				const void*					Data = nullptr;
				uint64						DataSize = 0;

				if (TensorInit)
				{
					DataSize = Graph->GetTensorDataSize(TensorInit);

					if (DataSize)
					{
						if (TensorDataBlob.Num() < DataSize)
						{
							TensorDataBlob.SetNumUninitialized(DataSize);
						}

						Graph->GetTensorData(TensorInit, TensorDataBlob.GetData(), DataSize, 0);
						Data = TensorDataBlob.GetData();
					}
				}

				auto Tensor =
					Builder->AddTensor(
						ANSI_TO_TCHAR(TensorInfo.name),
						DataType,
						MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen),
						Data, DataSize
					);

				Builder->AddOperatorInput(Op, Tensor);
			}

			for (int OutIdx = 0; OutIdx < NodeInfo.outputCount; ++OutIdx)
			{
				Ort::GraphTensorInfo		TensorInfo = Graph->GetNodeOutput(Node, OutIdx);
				ENNETensorDataType			DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);
				
				auto Tensor =
					Builder->AddTensor(
						ANSI_TO_TCHAR(TensorInfo.name),
						DataType,
						MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
					);

				Builder->AddOperatorOutput(Op, Tensor);
			}
		}

		return Builder->End(NNXData);
	}
};

/** Create a model optimizer */
NNXUTILS_API TUniquePtr<IModelOptimizer> CreateModelOptimizer(ENNXInferenceFormat InputFormat, ENNXInferenceFormat OutputFormat)
{
	if (InputFormat == ENNXInferenceFormat::ONNX)
	{
		if (OutputFormat == ENNXInferenceFormat::NNXRT)
		{
			return MakeUnique<FModelOptimizerONNXToNNXRT>();
		}
		else if (OutputFormat == ENNXInferenceFormat::ONNX)
		{
			return MakeUnique<FModelOptimizerONNXToONNX>();
		}
		else
		{
			return MakeUnique<FModelOptimizerONNXToORT>();
		}
	}

	//TODO jira 167592: Investigate how to conditionally compile the above,
	//removing the dependencies to ORT in runtime build (we will then
	//need a way to run tests without ability to create onnx
	//model at runtime). One could also make NNXUtils editor only
	//then the interface for optimization and validation should be in a
	//different module than the implementation.

	return nullptr;
}

} // namespace NNX
