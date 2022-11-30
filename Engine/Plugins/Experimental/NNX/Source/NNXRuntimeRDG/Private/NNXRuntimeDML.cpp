// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeRDG.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNXModelOptimizer.h"
#include "NNXOperator.h"

#include "NNECoreAttributeMap.h"

#include "HAL/FileManager.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "NNXShaderParameters.h"

#include "HAL/FileManager.h"

// NOTE: For now we only have DML on Windows, we should add support for XSX
#ifdef NNE_USE_DIRECTML

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <unknwn.h>
#include "Microsoft/COMPointer.h"
#include "DirectML.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

// DirectML is implemented using COM on all platforms
#ifdef IID_GRAPHICS_PPV_ARGS
#define DML_PPV_ARGS(x) __uuidof(*x), IID_PPV_ARGS_Helper(x)
#else
#define DML_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

#include "ID3D12DynamicRHI.h"
//#include "D3D12RHI/Private/D3D12Device.h"	// It could be great if we could get the FD3D12DescriptorHeapManager instance

namespace NNX
{

#define NNX_RUNTIME_DML_NAME TEXT("NNXRuntimeDml")

	
/**
 * Utility function to get operator type
 */
template<typename TElementWiseOpDesc>
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType();

#define OP_EW(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType<DML_ELEMENT_WISE_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ELEMENT_WISE_##OpName; }

#define OP_AN(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }


OP_EW(IDENTITY)
OP_EW(ABS)
OP_EW(ACOS)
OP_EW(ACOSH)
OP_EW(ASIN)
OP_EW(ASINH)
OP_EW(ATAN)
OP_EW(ATANH)
// BitShift
// Cast
OP_EW(CEIL)
OP_EW(CLIP)
OP_EW(COS)
OP_EW(COSH)
OP_AN(ELU)
OP_EW(ERF)
OP_EW(EXP)
OP_EW(FLOOR)
OP_EW(IS_INFINITY)
OP_EW(IS_NAN)
OP_AN(HARDMAX)
OP_AN(HARD_SIGMOID)
OP_AN(LEAKY_RELU)
OP_EW(LOG)
OP_EW(NEGATE)
// Not
OP_EW(RECIP)
OP_AN(RELU)
OP_EW(ROUND)
OP_AN(SCALED_ELU)
OP_AN(SIGMOID)
OP_EW(SIGN)
OP_EW(SIN)
OP_EW(SINH)
OP_AN(SOFTPLUS)
OP_AN(SOFTSIGN)
OP_EW(SQRT)
OP_EW(TAN)
OP_EW(TANH)

#undef OP_EW
#undef OP_AN


/**
 * Utility function to get operator type
 */
//template<typename TActivationOpDesc>
//DML_OPERATOR_TYPE GetDmlActivationOpType();
//
//#define OP(OpName) \
//template<> \
//DML_OPERATOR_TYPE GetDmlActivationOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }
//
//OP(ELU)
//OP(ERF)
//OP(HARDMAX)
//OP(HARD_SIGMOID)
//OP(LEAKY_RELU)
//OP(LINEAR)
//OP(LOG_SOFTMAX)
//OP(PARAMETERIZED_RELU)
//OP(PARAMETRIC_SOFTPLUS)
//OP(RELU)
//OP(SCALED_ELU)
//OP(SCALED_TANH)
//OP(SOFTMAX)
//OP(SOFTPLUS)
//OP(SOFTSIGN)
//OP(TANH)
//OP(THRESHOLDED_RELU)
//

/**
 * Utility function to get operator type
 */
template<typename TElementWiseOpDesc>
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType();

#define OP_EW(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType<DML_ELEMENT_WISE_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ELEMENT_WISE_##OpName; }

#define OP_AN(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }

OP_EW(ADD)
//OP_EW(LOGICAL_AND)
OP_EW(DIVIDE)
//OP_EW(LOGICAL_EQUALS)
//OP_EW(LOGICAL_GREATER_THAN)
//OP_EW(LOGICAL_LESS_THAN)
//OP_EW(MOD)
OP_EW(MULTIPLY)
//OP_EW(LOGICAL_OR)
OP_AN(PARAMETERIZED_RELU)
OP_EW(POW)
OP_EW(SUBTRACT)
//OP_EW(LOGICAL_XOR)

#undef OP_EW
#undef OP_AN

namespace DmlUtil
{
	struct FTensorDesc
	{
		DML_BUFFER_TENSOR_DESC										BuffDesc;
		DML_TENSOR_DESC												Desc;
		TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>>		Sizes;
		TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>>		Strides;
	};

	void SetTensorStrides(FTensorDesc& TensorDesc, const FTensor& InputDesc)
	{
		uint32 CurrStride = 1;

		TensorDesc.Strides.SetNum(InputDesc.GetShape().Rank());
		
		for (int32 i = InputDesc.GetShape().Rank() - 1; i >= 0; --i)
		{
			TensorDesc.Strides[i] = CurrStride;
			CurrStride *= InputDesc.GetShape().Data[i];
		}
	}

	void SetTensorSizesAndStridesForBroadcast(FTensorDesc& TensorDesc, const FTensor& InputDesc, const FTensor& TargetDesc)
	{
		static_assert(FTensorShape::MaxRank <= 8);
		
		const uint32 TargetDimension = TargetDesc.GetShape().Rank() != -1 ? TargetDesc.GetShape().Rank() : InputDesc.GetShape().Rank();
		checkf(TargetDesc.GetShape().Rank() >= InputDesc.GetShape().Rank(), TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), InputDesc.GetShape().Rank(), TargetDimension);
		
		TensorDesc.Sizes.SetNum(TargetDimension);
		TensorDesc.Strides.SetNum(TargetDimension);

		const int32 DimensionOffset = int32(TargetDimension - InputDesc.GetShape().Rank());
		
		for (int32 i = 0; i < (int32) TargetDimension; ++i)
		{
			TensorDesc.Sizes[i] = i < DimensionOffset ? 1 : InputDesc.GetShape().Data[i - DimensionOffset];
		}

		uint32 CurrStride = 1;

		for (int32 i = TargetDimension - 1; i >= 0; --i)
		{
			const bool bBroadcast = TensorDesc.Sizes[i] < TargetDesc.GetShape().Data[i];

			TensorDesc.Strides[i] = bBroadcast ? 0 : CurrStride;
			CurrStride *= TensorDesc.Sizes[i];

			TensorDesc.Sizes[i] = TargetDesc.GetShape().Data[i];
		}
	}

	inline bool IsSameShape(const FTensor& Left, const FTensor& Right)
	{
		if (Left.GetShape().Rank() != Right.GetShape().Rank())
		{
			return false;
		}
		
		for (int32 Idx = 0; Idx < Left.GetShape().Rank(); ++Idx)
		{
			if (Left.GetShape().Data[Idx] != Right.GetShape().Data[Idx])
			{
				return false;
			}
		}

		return true;
	}

	DML_TENSOR_DATA_TYPE GetTensorDataType(EMLTensorDataType DataType)
	{
		switch (DataType)
		{
		case EMLTensorDataType::Double:
			return DML_TENSOR_DATA_TYPE_FLOAT64;

		case EMLTensorDataType::Float:
			return DML_TENSOR_DATA_TYPE_FLOAT32;

		case EMLTensorDataType::Half:
			return DML_TENSOR_DATA_TYPE_FLOAT16;

		case EMLTensorDataType::UInt64:
			return DML_TENSOR_DATA_TYPE_UINT64;

		case EMLTensorDataType::UInt32:
			return DML_TENSOR_DATA_TYPE_UINT32;

		case EMLTensorDataType::UInt16:
			return DML_TENSOR_DATA_TYPE_UINT16;

		case EMLTensorDataType::UInt8:
			return DML_TENSOR_DATA_TYPE_UINT8;

		case EMLTensorDataType::Int64:
			return DML_TENSOR_DATA_TYPE_INT64;

		case EMLTensorDataType::Int32:
			return DML_TENSOR_DATA_TYPE_INT32;

		case EMLTensorDataType::Int16:
			return DML_TENSOR_DATA_TYPE_INT16;

		case EMLTensorDataType::Int8:
			return DML_TENSOR_DATA_TYPE_INT8;

		default:
			return DML_TENSOR_DATA_TYPE_UNKNOWN;
		}
	}

	inline uint64 CalculateBufferSize(const FTensorDesc& DmlTensor, const FTensor& Desc)
	{
		uint64 ElemSizeInBytes = Desc.GetElemByteSize();
		
		if (ElemSizeInBytes == 0)
		{
			return 0;
		}

		uint64 MinSizeInBytes = 0;		
		uint32 IndexOfLastElement = 0;

		for (int32 i = 0; i < DmlTensor.Sizes.Num(); ++i)
		{
			IndexOfLastElement += (DmlTensor.Sizes[i] - 1) * DmlTensor.Strides[i];
		}

		MinSizeInBytes = (static_cast<uint64>(IndexOfLastElement) + 1) * ElemSizeInBytes;
		
		// Round up to the nearest 4 bytes.
		MinSizeInBytes = (MinSizeInBytes + 3) & ~3ull;

		return MinSizeInBytes;
	}
}

//
//
//
//HACK: ATM we do not free the descriptors on inference model destruction, so we need to have a big pool until
//this is fixed. NNXQA Tests will still fail if run repeatedly in the same session until this is fixed.
constexpr const uint32 MaxNumDescriptors = 4096;


//
//
//
class FDeviceContextDml
{
public:

	uint32							DeviceIndex;
	ID3D12Device*					D3D12Device{ nullptr }; // Borrowed reference from RHI
	TComPtr<IDMLDevice>				Device{ nullptr };
	TComPtr<IDMLCommandRecorder>	CmdRec{ nullptr };
	TComPtr<ID3D12DescriptorHeap>	DescHeap{ nullptr };
	uint32							NumDescriptors{ 0 };
	uint32							DescriptorSize{ 0 };
};

//
// DirectML operator
//
class FMLOperatorDml : public FMLOperatorRDG
{
public:

	virtual bool Initialize(FDeviceContextDml* DevCtx, TArrayView<const FTensor> InputTensors, TArrayView<const FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) = 0;
	virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDG> InInputTensors, TConstArrayView<FTensorRDG> InOutputTensors) = 0;

protected:

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const FTensor& TensorDesc)
	{
		DML_TENSOR_DATA_TYPE DmlDataType = DmlUtil::GetTensorDataType(TensorDesc.GetDataType());

		if (DmlDataType == DML_TENSOR_DATA_TYPE_UNKNOWN)
		{
			DmlTensorDesc.BuffDesc = DML_BUFFER_TENSOR_DESC{};
			DmlTensorDesc.Desc = DML_TENSOR_DESC{};

			return false;
		}

		DmlTensorDesc.Sizes = TensorDesc.GetShape().Data;
		// TODO: Support tensor padding using strides defined in FMLTensorDesc
		//DmlUtil::SetTensorStrides(DmlTensorDesc, TensorDesc.Strides);
		
		DML_BUFFER_TENSOR_DESC& BuffDesc = DmlTensorDesc.BuffDesc;

		BuffDesc = DML_BUFFER_TENSOR_DESC{};

		BuffDesc.DataType = DmlDataType;
		BuffDesc.Flags = DML_TENSOR_FLAG_NONE;
		BuffDesc.DimensionCount = TensorDesc.GetShape().Rank();
		BuffDesc.Sizes = DmlTensorDesc.Sizes.GetData();
		BuffDesc.Strides = nullptr;
		BuffDesc.TotalTensorSizeInBytes = TensorDesc.GetDataSize();

		DmlTensorDesc.Desc = DML_TENSOR_DESC{ DML_TENSOR_TYPE_BUFFER, &DmlTensorDesc.BuffDesc };

		return true;
	}

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const FTensor& TensorDesc, const FTensor& BroadcastDesc)
	{
		DML_TENSOR_DATA_TYPE DmlDataType = DmlUtil::GetTensorDataType(TensorDesc.GetDataType());

		if (DmlDataType == DML_TENSOR_DATA_TYPE_UNKNOWN)
		{
			DmlTensorDesc.BuffDesc = DML_BUFFER_TENSOR_DESC{};
			DmlTensorDesc.Desc = DML_TENSOR_DESC{};

			return false;
		}

		if (DmlUtil::IsSameShape(TensorDesc, BroadcastDesc))
		{
			DmlTensorDesc.Sizes = TensorDesc.GetShape().Data;
			DmlUtil::SetTensorStrides(DmlTensorDesc, TensorDesc);
		}
		else if (TensorDesc.GetShape().Rank() > BroadcastDesc.GetShape().Rank())
		{
			return false;
		}
		else
		{
			DmlUtil::SetTensorSizesAndStridesForBroadcast(DmlTensorDesc, TensorDesc, BroadcastDesc);
		}

		//UE_LOG(LogNNX, Warning, TEXT("DmlTensorDesc:%d,%d,%d -> %d,%d,%d"),
		//	TensorDesc.Sizes[0],
		//	TensorDesc.Dimension > 1 ? TensorDesc.Sizes[1] : 0,
		//	TensorDesc.Dimension > 2 ? TensorDesc.Sizes[2] : 0,
		//	DmlTensorDesc.Sizes[0],
		//	DmlTensorDesc.Dimension > 1 ? DmlTensorDesc.Sizes[1] : 0,
		//	DmlTensorDesc.Dimension > 2 ? DmlTensorDesc.Sizes[2] : 0
		//);

		//UE_LOG(LogNNX, Warning, TEXT("DmlTensorStrides:%d,%d,%d"),
		//	DmlTensorDesc.Strides[0], DmlTensorDesc.Strides[1], DmlTensorDesc.Strides[2]
		//);

		check(DmlTensorDesc.Strides.Num() == DmlTensorDesc.Sizes.Num());
		
		DML_BUFFER_TENSOR_DESC& BuffDesc = DmlTensorDesc.BuffDesc;
		
		BuffDesc = DML_BUFFER_TENSOR_DESC{};

		BuffDesc.DataType = DmlDataType;
		BuffDesc.Flags = DML_TENSOR_FLAG_NONE;
		BuffDesc.DimensionCount = DmlTensorDesc.Sizes.Num();
		BuffDesc.Sizes = DmlTensorDesc.Sizes.GetData();
		BuffDesc.Strides = DmlTensorDesc.Strides.GetData();
		BuffDesc.TotalTensorSizeInBytes = DmlUtil::CalculateBufferSize(DmlTensorDesc, TensorDesc);
		
		DmlTensorDesc.Desc = DML_TENSOR_DESC{ DML_TENSOR_TYPE_BUFFER, &DmlTensorDesc.BuffDesc };

		return true;
	}
	
	bool CompileOperator(const DML_OPERATOR_DESC& DmlOpDesc)
	{
		IDMLDevice* Device = DevCtx->Device;

		// Create operator
		IDMLOperator* DmlOp = nullptr;

		HRESULT Res;
		
		Res = Device->CreateOperator(&DmlOpDesc, DML_PPV_ARGS(&DmlOp));
		if (!DmlOp)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to creat DML operator, hres:%d"), Res);
			return false;
		}

		// Compile operator
		Res = Device->CompileOperator(DmlOp, DML_EXECUTION_FLAG_NONE, DML_PPV_ARGS(&CompiledOp));
		if (!CompiledOp)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to compile DML operator"));
			return false;
		}

		// Initialize the operator
		IDMLCompiledOperator* DmlOps[] = { CompiledOp };
		TComPtr<IDMLOperatorInitializer>	DmlOpInit;

		Res = Device->CreateOperatorInitializer(1, DmlOps, DML_PPV_ARGS(&DmlOpInit));
		if (!DmlOpInit)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to create DML operator initializer"));
			return false;
		}

		DML_BINDING_PROPERTIES InitBindProps = DmlOpInit->GetBindingProperties();
		DML_BINDING_PROPERTIES ExecBindProps = CompiledOp->GetBindingProperties();

		// To create a descriptor heap we need the binding properties
		const uint32_t NumRequiredDescriptors = std::max(InitBindProps.RequiredDescriptorCount, ExecBindProps.RequiredDescriptorCount);

		if (DevCtx->NumDescriptors + NumRequiredDescriptors >= MaxNumDescriptors)
		{
			UE_LOG(LogNNX, Warning, TEXT("Maximum number of descriptors reached"));
			return false;
		}

		// Create a binding table over the descriptor heap we just created
		DML_BINDING_TABLE_DESC DmlBindingTableDesc{};

		DmlBindingTableDesc.Dispatchable = DmlOpInit;
		DmlBindingTableDesc.CPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(DevCtx->DescHeap->GetCPUDescriptorHandleForHeapStart(), DevCtx->NumDescriptors, DevCtx->DescriptorSize);
		DmlBindingTableDesc.GPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(DevCtx->DescHeap->GetGPUDescriptorHandleForHeapStart(), DevCtx->NumDescriptors, DevCtx->DescriptorSize);
		DmlBindingTableDesc.SizeInDescriptors = NumRequiredDescriptors;

		Res = DevCtx->Device->CreateBindingTable(&DmlBindingTableDesc, DML_PPV_ARGS(&BindingTable));
		if (!BindingTable)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to create DML binding table, res:%d"), Res);
			return false;
		}

		NumDescriptors = NumRequiredDescriptors;
		DescOffset = DevCtx->NumDescriptors;
		
		DevCtx->NumDescriptors += NumRequiredDescriptors;

		return true;
	}

	void ResetBindingTable(DML_BINDING_TABLE_DESC& Desc)
	{
		Desc.Dispatchable = CompiledOp;
		Desc.CPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(DevCtx->DescHeap->GetCPUDescriptorHandleForHeapStart(), DescOffset, DevCtx->DescriptorSize);
		Desc.GPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(DevCtx->DescHeap->GetGPUDescriptorHandleForHeapStart(), DescOffset, DevCtx->DescriptorSize);
		Desc.SizeInDescriptors = NumDescriptors;

		BindingTable->Reset(&Desc);
	}

	FDeviceContextDml*		DevCtx;

	IDMLCompiledOperator*	CompiledOp;
	IDMLBindingTable*		BindingTable;
	uint32					DescOffset;
	uint32					NumDescriptors;
	//uint64					TempMemSize;
	//uint64					ResidentMemSize;
};

/**
 * DirectML ML operator registry
 */
using FMLOperatorRegistryDml = TOperatorRegistryRDG<FMLOperatorDml>;

/**
 * Element-wise unary ML operator implementation
 */
template
<
	typename DmlElementWiseOpDescType, 
	EMLElementWiseUnaryOperatorType OpType
>
class FMLOperatorDmlElementWiseUnary : public FMLOperatorDml
{
public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlElementWiseUnary();
	}

	virtual ~FMLOperatorDmlElementWiseUnary() = default;

private:

	FMLOperatorDmlElementWiseUnary() : Alpha(0.0f), Beta(0.0f), Gamma(0.0f), Num(1) {}
	float Alpha;
	float Beta;
	float Gamma;
	uint32 Num;

public:

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const FTensor> InputTensors, TArrayView<const FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		// TODO: Setup attributes
		Num = InputTensors[0].GetVolume();

		DevCtx = InDevCtx;

		const FTensor& InputTensorDesc = InputTensors[0];
		const FTensor& OutputTensorDesc = OutputTensors[0];

		Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
		Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
		Gamma = Attributes.GetValueOrDefault(TEXT("gamma"), Gamma);

		// Initialize tensor descriptor (it's same for both input and output)
		DmlUtil::FTensorDesc	DmlTensorDesc{};

		if (!InitDmlTensorDesc(DmlTensorDesc, InputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseUnaryOpType<DmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		if (!CompileOperator(DmlOpDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to compile DML operator"));
			return false;
		}

		return true;
	}

private:

	template<typename OpDesc>
	void InitDmlOpDesc(OpDesc& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
	}

	void InitDmlOpDesc(DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Steepness = 1.0f;
	}

	void InitDmlOpDesc(DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
		Desc.Gamma = Gamma;
	}

	void InitDmlOpDesc(DML_ACTIVATION_ELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
	}

	void InitDmlOpDesc(DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
		Desc.Beta = Beta;
	}

	void InitDmlOpDesc(DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
	}

public:

	//
	//
	//
	virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDG> InInputTensors, TConstArrayView<FTensorRDG> InOutputTensors) override
	{
		ID3D12DynamicRHI* DynamicRHI = GetID3D12DynamicRHI();

		FMLElementWiseUnaryParameters* Params = GraphBuilder.AllocParameters<FMLElementWiseUnaryParameters>();

		Params->Input = InInputTensors[0].GetBuffer();
		Params->Output = InOutputTensors[0].GetBuffer();
		Params->Alpha = Alpha;
		Params->Beta = Beta;
		Params->Gamma = Gamma;
		Params->Num = Num;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLElementWiseUnaryDml_Transition"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputBuffer = Params->Input->GetRHI();
				FRHIBuffer* OutputBuffer = Params->Output->GetRHI();

				FRHITransitionInfo Transitions[] =
				{
					FRHITransitionInfo(InputBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(OutputBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
				};

				RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

				// FIXME: We need to flush commands here to transition the resources manually
				RHICmdList.SubmitCommandsHint();
			});

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLElementWiseUnaryDml_Dispatch"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[this, DynamicRHI, Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputBuffer = Params->Input->GetRHI();
				FRHIBuffer* OutputBuffer = Params->Output->GetRHI();

				// We need to defer the DML command list record
				RHICmdList.EnqueueLambda(
					[this, DynamicRHI, InputBuffer, OutputBuffer](FRHICommandListImmediate& RHICmdList)
					{
						DML_BINDING_TABLE_DESC DmlBindingTableDesc{};

						ResetBindingTable(DmlBindingTableDesc);

						// Get native resources for DML binding
						ID3D12Resource* InputResource = DynamicRHI->RHIGetResource(InputBuffer);
						ID3D12Resource* OutputResource = DynamicRHI->RHIGetResource(OutputBuffer);

						// TODO: Bind resources to DML binding table
						DML_BUFFER_BINDING	InputBuffBind{ InputResource, 0, InputResource->GetDesc().Width };
						DML_BUFFER_BINDING	OutputBuffBind{ OutputResource, 0, OutputResource->GetDesc().Width };

						DML_BINDING_DESC	InputBindDesc{ DML_BINDING_TYPE_BUFFER, &InputBuffBind };
						DML_BINDING_DESC	OutputBindDesc{ DML_BINDING_TYPE_BUFFER, &OutputBuffBind };

						BindingTable->BindInputs(1, &InputBindDesc);
						BindingTable->BindOutputs(1, &OutputBindDesc);

						// Record command list
						ID3D12GraphicsCommandList* CmdList = nullptr;

						CmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);

						// FIXME: Can't set resource barriers with FRHITransitionInfo
						//D3D12_RESOURCE_BARRIER PreResourceBarriers[] =
						//{
						//	CD3DX12_RESOURCE_BARRIER::Transition(
						//		InputResource,
						//		//D3D12_RESOURCE_STATE_COPY_DEST,
						//		D3D12_RESOURCE_STATE_COMMON,
						//		D3D12_RESOURCE_STATE_UNORDERED_ACCESS),

						//	CD3DX12_RESOURCE_BARRIER::Transition(
						//		OutputResource,
						//		D3D12_RESOURCE_STATE_COMMON,
						//		D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
						//};

						//CmdList->ResourceBarrier(2, PreResourceBarriers);

						CmdList->SetDescriptorHeaps(1, &DevCtx->DescHeap);
						DevCtx->CmdRec->RecordDispatch(CmdList, CompiledOp, BindingTable);

						//D3D12_RESOURCE_BARRIER PostResourceBarriers[] =
						//{
						//	CD3DX12_RESOURCE_BARRIER::UAV(OutputResource),

						//	CD3DX12_RESOURCE_BARRIER::Transition(
						//		OutputResource,
						//		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						//		D3D12_RESOURCE_STATE_COPY_SOURCE)
						//};

						//CmdList->ResourceBarrier(2, PostResourceBarriers);
					}
				);
			}
		);
	}
};

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::Selu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_ELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::Elu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(1.0f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::HardSigmoid>::FMLOperatorDmlElementWiseUnary()
	: Alpha(0.2f), Beta(0.5f), Gamma(0.0f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::LeakyRelu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(0.01f), Beta(0.0f), Gamma(0.0f), Num(1)
{
}

/**
 * Element-wise binary ML operator implementation
 */
template
<
	typename TDmlElementWiseOpDescType,
	EMLElementWiseBinaryOperatorType OpType
>
class FMLOperatorDmlElementWiseBinary : public FMLOperatorDml
{

public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlElementWiseBinary();
	}

private:

	FMLOperatorDmlElementWiseBinary() = default;

	uint32 Num { 1 };

public:

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const FTensor> InputTensors, TArrayView<const FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		// TODO: Setup attributes
		Num = OutputTensors[0].GetVolume();

		DevCtx = InDevCtx;

		const FTensor& InputATensorDesc = InputTensors[0];
		const FTensor& InputBTensorDesc = InputTensors[1];
		const FTensor& OutputTensorDesc = OutputTensors[0];

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputATensorDesc{};
		DmlUtil::FTensorDesc	DmlInputBTensorDesc{};
		DmlUtil::FTensorDesc	DmlOutputTensorDesc{};

		if (!InitDmlTensorDesc(DmlInputATensorDesc, InputATensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlInputBTensorDesc, InputBTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlOutputTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		TDmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlInputATensorDesc, DmlInputBTensorDesc, DmlOutputTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseBinaryOpType<TDmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		if (!CompileOperator(DmlOpDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to compile DML operator"));
			return false;
		}

		return true;
	}

private:

	template<typename OpDesc>
	void InitDmlOpDesc(OpDesc& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.ATensor = &LHSTensor.Desc;
		Desc.BTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}

	void InitDmlOpDesc(DML_ELEMENT_WISE_POW_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.InputTensor = &LHSTensor.Desc;
		Desc.ExponentTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}

	void InitDmlOpDesc(DML_ACTIVATION_PARAMETERIZED_RELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.InputTensor = &LHSTensor.Desc;
		Desc.SlopeTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}

public:

	//
	//
	//
	virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDG> InInputTensors, TConstArrayView<FTensorRDG> InOutputTensors) override
	{
		ID3D12DynamicRHI* DynamicRHI = GetID3D12DynamicRHI();

		FMLElementWiseBinaryParameters* Params = GraphBuilder.AllocParameters<FMLElementWiseBinaryParameters>();

		Params->LHSInput = InInputTensors[0].GetBuffer();
		Params->RHSInput = InInputTensors[1].GetBuffer();
		Params->Output = InOutputTensors[0].GetBuffer();
		Params->Num = Num;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLElementWiseBinaryDml_Transition"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputABuffer = Params->LHSInput->GetRHI();
				FRHIBuffer* InputBBuffer = Params->RHSInput->GetRHI();
				FRHIBuffer* OutputBuffer = Params->Output->GetRHI();

				FRHITransitionInfo Transitions[] =
				{
					FRHITransitionInfo(InputABuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(InputBBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(OutputBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
				};

				RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

				// FIXME: We need to flush commands here to transition the resources manually
				RHICmdList.SubmitCommandsHint();
			});

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLElementWiseBinaryDml_Dispatch"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[this, DynamicRHI, Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputBufferA = Params->LHSInput->GetRHI();
				FRHIBuffer* InputBufferB = Params->RHSInput->GetRHI();
				FRHIBuffer* OutputBuffer = Params->Output->GetRHI();

				// We need to defer the DML command list record
				RHICmdList.EnqueueLambda(
					[this, DynamicRHI, InputBufferA, InputBufferB, OutputBuffer](FRHICommandListImmediate& RHICmdList)
					{
						DML_BINDING_TABLE_DESC DmlBindingTableDesc{};

						ResetBindingTable(DmlBindingTableDesc);

						// Get native resources for DML binding
						ID3D12Resource* InputAResource = DynamicRHI->RHIGetResource(InputBufferA);
						ID3D12Resource* InputBResource = DynamicRHI->RHIGetResource(InputBufferB);
						ID3D12Resource* OutputResource = DynamicRHI->RHIGetResource(OutputBuffer);

						// TODO: Bind resources to DML binding table
						DML_BUFFER_BINDING	InputABuffBind = { InputAResource, 0, InputAResource->GetDesc().Width };
						DML_BUFFER_BINDING	InputBBuffBind = { InputBResource, 0, InputBResource->GetDesc().Width };
						DML_BUFFER_BINDING	OutputBuffBind{ OutputResource, 0, OutputResource->GetDesc().Width };

						DML_BINDING_DESC	InputABindDesc{ DML_BINDING_TYPE_BUFFER, &InputABuffBind };
						DML_BINDING_DESC	InputBBindDesc{ DML_BINDING_TYPE_BUFFER, &InputBBuffBind };
						DML_BINDING_DESC	OutputBindDesc{ DML_BINDING_TYPE_BUFFER, &OutputBuffBind };

						DML_BINDING_DESC	InputBindings[] = { InputABindDesc, InputBBindDesc };

						BindingTable->BindInputs(2, InputBindings);
						BindingTable->BindOutputs(1, &OutputBindDesc);

						// Record command list
						ID3D12GraphicsCommandList* CmdList = nullptr;

						CmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);

						CmdList->SetDescriptorHeaps(1, &DevCtx->DescHeap);
						DevCtx->CmdRec->RecordDispatch(CmdList, CompiledOp, BindingTable);

					}
				);
			}
		);
	}
};

/**
 * Gemm
 */
class FMLOperatorDmlGemm : public FMLOperatorDml
{
public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlGemm();
	}

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const FTensor> InputTensors, TArrayView<const FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		// Setup attributes
		float Alpha = 1.0f;
		float Beta = 1.0f;
		int32 TransA = 0;
		int32 TransB = 0;
		
		Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
		Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
		TransA = Attributes.GetValueOrDefault(TEXT("transA"), TransA);
		TransB = Attributes.GetValueOrDefault(TEXT("transB"), TransB);
		
		DevCtx = InDevCtx;

		const FTensor& InputATensorDesc = InputTensors[0];
		const FTensor& InputBTensorDesc = InputTensors[1];
		const FTensor& OutputTensorDesc = OutputTensors[0];

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputATensorDesc{};
		DmlUtil::FTensorDesc	DmlInputBTensorDesc{};
		DmlUtil::FTensorDesc	DmlInputCTensorDesc{};
		DmlUtil::FTensorDesc	DmlOutputTensorDesc{};

		if (!InitDmlTensorDesc(DmlInputATensorDesc, InputATensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlInputBTensorDesc, InputBTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (InputTensors.Num() > 2)
		{
			const FTensor& InputCTensorDesc = InputTensors[2];

			if (!InitDmlTensorDesc(DmlInputCTensorDesc, InputCTensorDesc, OutputTensorDesc))
			{
				UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
				return false;
			}
		}

		if (!InitDmlTensorDesc(DmlOutputTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DML_GEMM_OPERATOR_DESC	DmlGemmOpDesc{};

		DmlGemmOpDesc.ATensor = &DmlInputATensorDesc.Desc;
		DmlGemmOpDesc.BTensor = &DmlInputBTensorDesc.Desc;
		DmlGemmOpDesc.CTensor = InputTensors.Num() > 2 ? &DmlInputCTensorDesc.Desc : nullptr;
		DmlGemmOpDesc.OutputTensor = &DmlOutputTensorDesc.Desc;
		DmlGemmOpDesc.Alpha = Alpha;
		DmlGemmOpDesc.Beta = Beta;
		DmlGemmOpDesc.TransA = TransA ? DML_MATRIX_TRANSFORM_TRANSPOSE : DML_MATRIX_TRANSFORM_NONE;
		DmlGemmOpDesc.TransB = TransB ? DML_MATRIX_TRANSFORM_TRANSPOSE : DML_MATRIX_TRANSFORM_NONE;

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = DML_OPERATOR_GEMM;
		DmlOpDesc.Desc = &DmlGemmOpDesc;

		if (!CompileOperator(DmlOpDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to compile DML operator"));
			return false;
		}

		return true;
	}

	//
	//
	//
	virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDG> InInputTensors, TConstArrayView<FTensorRDG> InOutputTensors) override
	{
		ID3D12DynamicRHI* DynamicRHI = GetID3D12DynamicRHI();

		const int32 NumInputs = InInputTensors.Num();
		const bool bIsUsingBias = NumInputs > 2;

		FMLGemmParameters* Params = GraphBuilder.AllocParameters<FMLGemmParameters>();

		Params->A = InInputTensors[0].GetBuffer();
		Params->B = InInputTensors[1].GetBuffer();
		Params->C = bIsUsingBias ? InInputTensors[2].GetBuffer() : nullptr;
		Params->Y = InOutputTensors[0].GetBuffer();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLGemmDml_Transition"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputABuffer = Params->A->GetRHI();
				FRHIBuffer* InputBBuffer = Params->B->GetRHI();
				FRHIBuffer* InputCBuffer = Params->C ? Params->C->GetRHI() : nullptr;
				FRHIBuffer* OutputBuffer = Params->Y->GetRHI();

				FRHITransitionInfo Transitions[] =
				{
					FRHITransitionInfo(InputABuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(InputBBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(OutputBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
					FRHITransitionInfo(InputCBuffer, ERHIAccess::Unknown, ERHIAccess::UAVCompute)
				};

				RHICmdList.Transition(MakeArrayView(Transitions, Params->C ? 4 : 3));

				// FIXME: We need to flush commands here to transition the resources manually
				RHICmdList.SubmitCommandsHint();
			});

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLGemmDml_Dispatch"),
			Params,
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			[this, DynamicRHI, Params](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* InputBufferA = Params->A->GetRHI();
				FRHIBuffer* InputBufferB = Params->B->GetRHI();
				FRHIBuffer* InputBufferC = Params->C ? Params->C->GetRHI() : nullptr;
				FRHIBuffer* OutputBuffer = Params->Y->GetRHI();

				// We need to defer the DML command list record
				RHICmdList.EnqueueLambda(
					[this, DynamicRHI, InputBufferA, InputBufferB, InputBufferC, OutputBuffer](FRHICommandListImmediate& RHICmdList)
					{
						DML_BINDING_TABLE_DESC DmlBindingTableDesc{};

						ResetBindingTable(DmlBindingTableDesc);

						// Get native resources for DML binding
						ID3D12Resource* InputAResource = DynamicRHI->RHIGetResource(InputBufferA);
						ID3D12Resource* InputBResource = DynamicRHI->RHIGetResource(InputBufferB);
						ID3D12Resource* InputCResource = InputBufferC ? DynamicRHI->RHIGetResource(InputBufferC) : nullptr;
						ID3D12Resource* OutputResource = DynamicRHI->RHIGetResource(OutputBuffer);

						// TODO: Bind resources to DML binding table
						DML_BUFFER_BINDING	InputABuffBind { InputAResource, 0, InputAResource->GetDesc().Width };
						DML_BUFFER_BINDING	InputBBuffBind { InputBResource, 0, InputBResource->GetDesc().Width };
						DML_BUFFER_BINDING	InputCBuffBind { InputCResource, 0, InputCResource ? InputCResource->GetDesc().Width : 0 };
						DML_BUFFER_BINDING	OutputBuffBind { OutputResource, 0, OutputResource->GetDesc().Width };

						DML_BINDING_DESC	InputABindDesc{ DML_BINDING_TYPE_BUFFER, &InputABuffBind };
						DML_BINDING_DESC	InputBBindDesc{ DML_BINDING_TYPE_BUFFER, &InputBBuffBind };
						DML_BINDING_DESC	InputCBindDesc{ DML_BINDING_TYPE_BUFFER, &InputCBuffBind };
						DML_BINDING_DESC	OutputBindDesc{ DML_BINDING_TYPE_BUFFER, &OutputBuffBind };

						DML_BINDING_DESC	InputBindings[] = { InputABindDesc, InputBBindDesc, InputCBindDesc };

						BindingTable->BindInputs(3, InputBindings);
						BindingTable->BindOutputs(1, &OutputBindDesc);

						// Record command list
						ID3D12GraphicsCommandList* CmdList = nullptr;

						CmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);

						CmdList->SetDescriptorHeaps(1, &DevCtx->DescHeap);
						DevCtx->CmdRec->RecordDispatch(CmdList, CompiledOp, BindingTable);

					}
				);
			}
		);
	}
};

//
//
//
class FMLInferenceModelDml : public FMLInferenceModelRDG
{
public:

	FMLInferenceModelDml();
	~FMLInferenceModelDml();

	bool Init(TConstArrayView<uint8> ModelData, FDeviceContextDml* InDevCtx);

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override;
	virtual int RunShapeInference() override;

private:

	FMLOperatorDml* OpCreate(const FString& Name, TArrayView<const FTensor> InputTensorDesc, TArrayView<const FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes);
	
	TArray<FMLOperatorDml*>		Operators;
	FDeviceContextDml*			DevCtx;
};

//
//
//
class FMLRuntimeDml : public FMLRuntimeRDG
{
public:

	FMLRuntimeDml() = default;
	virtual ~FMLRuntimeDml();

	virtual FString GetRuntimeName() const override
	{
		return NNX_RUNTIME_DML_NAME;
	}
	
	virtual EMLRuntimeSupportFlags GetSupportFlags() const override
	{
		return EMLRuntimeSupportFlags::RDG;
	}

	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) override
	{
		if (!CanCreateModelData(FileType, FileData))
		{
			return {};
		}

		TUniquePtr<IModelOptimizer> Optimizer = CreateONNXToNNXModelOptimizer();

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

	virtual TUniquePtr<FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData) override
	{
		if (!CanCreateModel(ModelData))
		{
			return TUniquePtr<FMLInferenceModel>();
		}

		// Create the model and initialize it with the data not including the header
		FMLInferenceModelDml* Model = new FMLInferenceModelDml();
		if (!Model->Init(ModelData, &Ctx))
		{
			delete Model;
			return TUniquePtr<FMLInferenceModel>();
		}
		return TUniquePtr<FMLInferenceModel>(Model);
	}

	bool Init();

private:

	bool RegisterElementWiseUnaryOperators();
	bool RegisterElementWiseBinaryOperators();
	bool RegisterGemmOperator();
	
	FDeviceContextDml		Ctx;
};

//
//
//
static TUniquePtr<FMLRuntimeDml> GDmlRuntime;

//
//
//
FMLRuntimeDml::~FMLRuntimeDml()
{
}

//
//
//
bool FMLRuntimeDml::Init()
{
	HRESULT Res;

	// In order to use DirectML we need D3D12
	ID3D12DynamicRHI* RHI = nullptr;

	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12)
	{
		RHI = static_cast<ID3D12DynamicRHI*>(GDynamicRHI);

		if (!RHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
	}
	else
	{
		if (GDynamicRHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:No RHI found"));
			return false;
		}
	}

	RegisterElementWiseUnaryOperators();
	RegisterElementWiseBinaryOperators();
	RegisterGemmOperator();

	Ctx.DeviceIndex = 0;
	Ctx.D3D12Device = RHI->RHIGetDevice(Ctx.DeviceIndex);

#if PLATFORM_WINDOWS
	ID3D12Device5* D3D12Device5 = nullptr;
	
	Res = Ctx.D3D12Device->QueryInterface(&D3D12Device5);
	if (D3D12Device5)
	{
		uint32	NumCommands = 0;

		Res = D3D12Device5->EnumerateMetaCommands(&NumCommands, nullptr);
		if (NumCommands)
		{
			UE_LOG(LogNNX, Verbose, TEXT("D3D12 Meta commands:%u"), NumCommands);

			TArray<D3D12_META_COMMAND_DESC>	MetaCmds;

			MetaCmds.SetNumUninitialized(NumCommands);

			Res = D3D12Device5->EnumerateMetaCommands(&NumCommands, MetaCmds.GetData());
			for (uint32 Idx = 0; Idx < NumCommands; ++Idx)
			{
				const D3D12_META_COMMAND_DESC& Desc = MetaCmds[Idx];

				UE_LOG(LogNNX, Verbose, TEXT("   %s"), Desc.Name);
			}
		}
	}
#endif

	DML_CREATE_DEVICE_FLAGS DmlCreateFlags = DML_CREATE_DEVICE_FLAG_NONE;

	// Set debugging flags
	if (RHI->IsD3DDebugEnabled())
	{
		DmlCreateFlags |= DML_CREATE_DEVICE_FLAG_DEBUG;
	}

	Res = DMLCreateDevice(Ctx.D3D12Device, DmlCreateFlags, DML_PPV_ARGS(&Ctx.Device));
	if (!Ctx.Device)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create DML device, res:%x"), Res);
		return false;
	}
	
	Res = Ctx.Device->CreateCommandRecorder(DML_PPV_ARGS(&Ctx.CmdRec));
	if (!Ctx.CmdRec)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create DML command recorder, res:%x"), Res);
		return false;
	}

	// TODO: Use D3D12RHI descriptor heaps
	D3D12_DESCRIPTOR_HEAP_DESC	HeapDesc = {};

	HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	HeapDesc.NumDescriptors = MaxNumDescriptors;

	Res = Ctx.D3D12Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&Ctx.DescHeap));
	if (!Ctx.DescHeap)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create D3D12 descriptor heap, res:%x"), Res);
		return false;
	}

	Ctx.DescriptorSize = Ctx.D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterElementWiseUnaryOperators()
{
#define OP(DmlOpDesc, OpName) FMLOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FMLOperatorDmlElementWiseUnary<DmlOpDesc, EMLElementWiseUnaryOperatorType::OpName>::Create)

	OP(DML_ELEMENT_WISE_ABS_OPERATOR_DESC, Abs);
	OP(DML_ELEMENT_WISE_ACOS_OPERATOR_DESC, Acos);
	OP(DML_ELEMENT_WISE_ACOSH_OPERATOR_DESC, Acosh);
	OP(DML_ELEMENT_WISE_ASIN_OPERATOR_DESC, Asin);
	OP(DML_ELEMENT_WISE_ASINH_OPERATOR_DESC, Asinh);
	OP(DML_ELEMENT_WISE_ATAN_OPERATOR_DESC, Atan);
	OP(DML_ELEMENT_WISE_ATANH_OPERATOR_DESC, Atanh);
	OP(DML_ELEMENT_WISE_CEIL_OPERATOR_DESC, Ceil);
	OP(DML_ELEMENT_WISE_COS_OPERATOR_DESC, Cos);
	OP(DML_ELEMENT_WISE_COSH_OPERATOR_DESC, Cosh);
	OP(DML_ACTIVATION_ELU_OPERATOR_DESC, Elu);
	OP(DML_ELEMENT_WISE_ERF_OPERATOR_DESC, Erf);
	OP(DML_ELEMENT_WISE_EXP_OPERATOR_DESC, Exp);
	OP(DML_ELEMENT_WISE_FLOOR_OPERATOR_DESC, Floor);
	OP(DML_ELEMENT_WISE_IS_INFINITY_OPERATOR_DESC, IsInf);
	OP(DML_ELEMENT_WISE_IS_NAN_OPERATOR_DESC, IsNan);
	OP(DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC, HardSigmoid);
	//OP(HardSwish);
	OP(DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC, LeakyRelu);
	OP(DML_ELEMENT_WISE_LOG_OPERATOR_DESC, Log);
	OP(DML_ELEMENT_WISE_NEGATE_OPERATOR_DESC, Neg);
	//OP(Not);
	OP(DML_ELEMENT_WISE_RECIP_OPERATOR_DESC, Reciprocal);
	OP(DML_ACTIVATION_RELU_OPERATOR_DESC, Relu);
	OP(DML_ELEMENT_WISE_ROUND_OPERATOR_DESC, Round);
	OP(DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC, Selu);
	OP(DML_ACTIVATION_SIGMOID_OPERATOR_DESC, Sigmoid);
	OP(DML_ELEMENT_WISE_SIGN_OPERATOR_DESC, Sign);
	OP(DML_ELEMENT_WISE_SIN_OPERATOR_DESC, Sin);
	OP(DML_ELEMENT_WISE_SINH_OPERATOR_DESC, Sinh);
	OP(DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC, Softplus);
	OP(DML_ACTIVATION_SOFTSIGN_OPERATOR_DESC, Softsign);
	OP(DML_ELEMENT_WISE_SQRT_OPERATOR_DESC, Sqrt);
	OP(DML_ELEMENT_WISE_TAN_OPERATOR_DESC, Tan);
	OP(DML_ELEMENT_WISE_TANH_OPERATOR_DESC, Tanh);

#undef OP

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterElementWiseBinaryOperators()
{
#define OP(DmlOpDesc, OpName) FMLOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FMLOperatorDmlElementWiseBinary<DmlOpDesc, EMLElementWiseBinaryOperatorType::OpName>::Create)

	OP(DML_ELEMENT_WISE_ADD_OPERATOR_DESC, Add);
	// And
	OP(DML_ELEMENT_WISE_DIVIDE_OPERATOR_DESC, Div);
	// Equal
	// Greater
	// GreaterOrEqual
	// Less
	// LessOrEqual
	// Mod
	OP(DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC, Mul);
	// Or
	OP(DML_ACTIVATION_PARAMETERIZED_RELU_OPERATOR_DESC, Prelu);
	OP(DML_ELEMENT_WISE_POW_OPERATOR_DESC, Pow);
	OP(DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC, Sub);
	// Xor

#undef OP

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterGemmOperator()
{
	FMLOperatorRegistryDml::Get()->OpAdd(TEXT("Gemm"), FMLOperatorDmlGemm::Create);
	return true;
}

//
//
//
FMLInferenceModelDml::FMLInferenceModelDml()
{
	bUseManualTransitions = true;
}

//
//
//
FMLInferenceModelDml::~FMLInferenceModelDml()
{
	// TODO: Release all the operators
}

//
//
//
bool FMLInferenceModelDml::Init(TConstArrayView<uint8> ModelData, FDeviceContextDml* InDevCtx)
{
	check(ModelData.Num() > 0);
	FMLRuntimeFormat	Format;

	if (!LoadModel(ModelData, Format))
	{
		return false;
	}

	DevCtx = InDevCtx;

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		TArray<FTensor> OpInputTensors;
		TArray<FTensor> OpOutputTensors;
		UE::NNECore::FAttributeMap AttributeMap;

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			FTensorDesc SymbolicTensorDesc = AllSymbolicTensorDescs[InputTensorIndex];
			//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
			OpInputTensors.Emplace(FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
		}
		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			FTensorDesc SymbolicTensorDesc = AllSymbolicTensorDescs[OutputTensorIndex];
			//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
			OpOutputTensors.Emplace(FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
		}
		for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		FMLOperatorDml* Op = OpCreate(TypeName, OpInputTensors, OpOutputTensors, AttributeMap);

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
void FMLInferenceModelDml::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder)
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

		FMLOperatorDml* Op = Operators[Idx];

		Op->Dispatch(GraphBuilder, InputTensors, OutputTensors);
	}
}

//
// Create operator
//
FMLOperatorDml* FMLInferenceModelDml::OpCreate(const FString& OpName, TArrayView<const FTensor> InputTensorDescs, TArrayView<const FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes)
{
	// TODO: Check if D3D12 device is valid

	FMLOperatorRegistryDml::OperatorCreateFunc CreateFn = FMLOperatorRegistryDml::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNX, Warning, TEXT("Dml MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	FMLOperatorDml*	Op = CreateFn();

	if (!Op->Initialize(DevCtx, InputTensorDescs, OutputTensorDescs, Attributes))
	{
		delete Op;

		UE_LOG(LogNNX, Warning, TEXT("Error:Failed to initialize operator:%s"), *OpName);
		return nullptr;
	}

	return Op;
}

//
//
//
int FMLInferenceModelDml::RunShapeInference()
{
	AllShapes.Empty();

	for (FTensorDesc SymbolicTensorDesc : AllSymbolicTensorDescs)
	{
		if (SymbolicTensorDesc.IsConcrete())
		{
			FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(SymbolicTensorDesc.GetShape());
			AllShapes.Emplace(TensorShape);
		}
	}
	if (AllShapes.Num() != AllSymbolicTensorDescs.Num())
	{
		AllShapes.Empty();
		UE_LOG(LogNNX, Warning, TEXT("DML engine does not support model with variable shapes yet."));
		return -1;
	}

	return 0;
}

//
//
//
static TUniquePtr<FMLRuntimeDml> FDmlRuntimeCreate()
{
	auto Runtime = MakeUnique<FMLRuntimeDml>();

	if (!Runtime->Init())
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create NNX DML runtime"));
		Runtime.Release();
	}

	return Runtime;
}

//
// Called on RDG runtime startup
//
IRuntime* FMLRuntimeDmlStartup()
{
	if (!GDmlRuntime)
	{
		//const FString ModulesDir = FPlatformProcess::GetModulesDirectory();;
		//const FString GameDir = FModuleManager::Get().GetGameBinariesDirectory();
		//const FString PluginDir = FPlatformProcess::BaseDir();

#ifdef DIRECTML_BIN_PATH

		//const FString PluginDir = IPluginManager::Get().FindPlugin(TEXT("NeuralNetworkInference"))->GetBaseDir();
		const FString DirectMLRuntimeBinPath = TEXT(PREPROCESSOR_TO_STRING(DIRECTML_BIN_PATH));
		//const FString DirectMLDLLPath = DirectMLRuntimeBinPath / TEXT(PREPROCESSOR_TO_STRING(DIRECTML_DLL_NAME));
		
		FPlatformProcess::PushDllDirectory(*DirectMLRuntimeBinPath);
		
		const FString DirectMLDLLPaths[] =
		{
			DirectMLRuntimeBinPath / TEXT("DirectML.Debug.dll"),
			DirectMLRuntimeBinPath / TEXT("DirectML.dll"),
		};

		// Sanity check
		for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(DirectMLDLLPaths); ++Idx)
		{
			const FString& DirectMLDLLPath = DirectMLDLLPaths[Idx];

			if (!FPaths::FileExists(DirectMLDLLPath))
			{
				const FString ErrorMessage = FString::Format(TEXT("DirectML DLL file not found in \"{0}\"."),
					{ IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*DirectMLDLLPath) });
				UE_LOG(LogNNX, Warning, TEXT("NNXRuntimeDll:%s"), *ErrorMessage);
				checkf(false, TEXT("%s"), *ErrorMessage);
			}

			FPlatformProcess::GetDllHandle(*DirectMLDLLPath);
		}

		FPlatformProcess::PopDllDirectory(*DirectMLRuntimeBinPath);
#endif

//#if DEBUG
		//FPlatformProcess::GetDllHandle(TEXT("DirectML.Debug.dll"));
//#else
//		FPlatformProcess::GetDllHandle(TEXT("DirectML.dll"));
//#endif
		GDmlRuntime = FDmlRuntimeCreate();
	}

	return GDmlRuntime.Get();
}

//
// Called on RDG runtime shutdown
//
void FMLRuntimeDmlShutdown()
{
	if (GDmlRuntime)
	{
		GDmlRuntime.Release();
	}
}


} // namespace NNX
 
#endif
