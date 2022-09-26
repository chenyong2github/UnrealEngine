// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXModelOptimizer.h"
#include "NNXCore.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelBuilder.h"

#ifdef PLATFORM_WIN64
#define ORT_STRING_CAST TCHAR_TO_WCHAR
#else
#define ORT_STRING_CAST TCHAR_TO_ANSI
#endif

#include "NNXThirdPartyWarningDisabler.h"
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/ort_model_optimizer_api.h"
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

		if (Tensor.shapeLen == 0)
		{
			Str.Appendf("%d", (int) Tensor.shape[0]);
		}
		else 
		{
			for (int c = 0; c < Tensor.shapeLen; ++c) 
			{
				if (Tensor.shape[c] == 0)
					Str.Append("N");
				else
					Str.Appendf("%d", (int) Tensor.shape[c]);

				Str.Appendf("%c", c < Tensor.shapeLen - 1 ? ',' : ' ');
			}
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
				int64 NElems = 10;

				if (Tensor.shapeLen == 1) 
				{
					if (Tensor.shape[0] < NElems)
					{
                        NElems = Tensor.shape[0];
					}
				} 
				else 
				{
                    const int64_t dim = Tensor.shape[Tensor.shapeLen - 1];

					if (dim < NElems)
					{
						NElems = dim;
					}
				}

				for (int64_t ec = 0; ec < NElems; ++ec) 
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

	static void OnLog(const char* LogMsg)
	{
		UE_LOG(LogNNX, Warning, TEXT("%s"), ANSI_TO_TCHAR(LogMsg));
	}

private:

	TUniquePtr<Ort::IModelGraph>	Graph;
	TArray<uint8>					Storage;
};

#undef Print


#define CASE(GType, Type) case Ort::GraphTensorDataType::GType: return EMLTensorDataType::Type

EMLTensorDataType GetDataTypeFromGraphTensor(Ort::GraphTensorDataType TensorDataType)
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
			return EMLTensorDataType::None;
	}
}

#undef CASE

namespace NNX
{

class FMLModelOptimizerONNXToNNX : public IMLModelOptimizer
{
public:

	FMLModelOptimizerONNXToNNX()
	{
	}

	virtual bool Optimize(TArrayView<uint8> ONNXData, TArray<uint8>& NNXData) override
	{
		Ort::ModelOptimizeOptions	Opts{};
		
		Opts.logCallback = OnLog;

		TUniquePtr<Ort::IModelGraph> Graph(OrtOptimizeModelFromMemory(ONNXData.GetData(), ONNXData.Num(), Opts));
		if (!Graph)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to load ONNX model from memory"));
			return false;
		}

		//auto Printer = MakeUnique<FModelGraphPrinter>(Graph.Get());
		//Printer->Run();
		
		return BuildNNXFormat(Graph.Get(), NNXData);
	}

	bool BuildNNXFormat(Ort::IModelGraph* Graph, TArray<uint8>& NNXData)
	{
		TUniquePtr<NNX::IMLModelBuilder>	Builder(NNX::CreateNNXModelBuilder());

		Ort::GraphInfo GraphInfo = Graph->GetGraphInfo();

		Builder->Begin(GraphInfo.name);

		// Add tensors for graph inputs
		for (int Idx = 0; Idx < GraphInfo.inputCount; ++Idx)
		{
			Ort::GraphTensorInfo TensorInfo = Graph->GetGraphInput(Idx);

			EMLTensorDataType DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);

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

			EMLTensorDataType DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);

			auto Tensor =
				Builder->AddTensor(
					ANSI_TO_TCHAR(TensorInfo.name),
					DataType,
					MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
				);

			Builder->AddOutput(Tensor);
		}

		// Traverse all the nodes get their inputs, outputs and tensor data
		for (int Idx = 0; Idx < GraphInfo.nodeCount; ++Idx)
		{
			Ort::GraphNode		Node = Graph->GetNode(Idx);
			Ort::GraphNodeInfo	NodeInfo = Graph->GetNodeInfo(Node);

			auto Op = Builder->AddOperator(NodeInfo.opName);

			for (int InIdx = 0; InIdx < NodeInfo.inputCount; ++InIdx)
			{
				Ort::GraphTensorInfo		TensorInfo = Graph->GetNodeInput(Node, InIdx);
				Ort::GraphTensorInitializer	Init = Graph->GetTensorInitializer(TensorInfo.name);
				EMLTensorDataType			DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);
				const void*					Data = nullptr;
				uint64						DataSize = 0;

				// TODO: Use tensor initializer data
				if (!Init)
				{
					
				}
				
				auto Tensor =
					Builder->AddTensor(
						ANSI_TO_TCHAR(TensorInfo.name),
						DataType,
						MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
					);

				Builder->AddOperatorInput(Op, Tensor);
			}

			for (int OutIdx = 0; OutIdx < NodeInfo.outputCount; ++OutIdx)
			{
				Ort::GraphTensorInfo		TensorInfo = Graph->GetNodeOutput(Node, OutIdx);
				EMLTensorDataType			DataType = GetDataTypeFromGraphTensor(TensorInfo.dataType);
				
				auto Tensor =
					Builder->AddTensor(
						ANSI_TO_TCHAR(TensorInfo.name),
						DataType,
						MakeArrayView(TensorInfo.shape, TensorInfo.shapeLen)
					);

				Builder->AddOperatorOutput(Op, Tensor);
			}
		}

		//// Traverse all tensor initializers
		//for (int Idx = 0; Idx < GraphInfo.tensorInitializerCount; ++Idx)
		//{
		//	Ort::GraphTensorInitializer TensorInit = Graph->GetTensorInitializer(Idx);

		//	Builder.AddTensor();
		//}

		return Builder->End(NNXData);
	}

	static void OnLog(const char* LogMsg)
	{
		UE_LOG(LogNNX, Warning, TEXT("%s"), ANSI_TO_TCHAR(LogMsg));
	}
};

/** Create a model optimizer */
NNXUTILS_API IMLModelOptimizer* CreateModelOptimizer(EMLInferenceFormat InputFormat, EMLInferenceFormat OutputFormat)
{
	if (InputFormat == EMLInferenceFormat::ONNX)
	{
		if (OutputFormat == EMLInferenceFormat::NNXRT)
		{
			return new FMLModelOptimizerONNXToNNX();
		}
	}

	return nullptr;
}

} // namespace NNX
