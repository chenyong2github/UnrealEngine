// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGElementWiseVariadic.h"
#include "NNEHlslShadersElementWiseVariadicCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorElementWiseVariadic, TEXT("NNI.Operator.Hlsl.ElementWise.Variadic"));

	using TElementWiseVariadicCS = typename UE::NNEHlslShaders::Internal::TElementWiseVariadicCS;
	using FElementWiseVariadicConstants = UE::NNEHlslShaders::Internal::FElementWiseVariadicConstants;

	void AddOneVariadicOpPass(FRDGBuilder& GraphBuilder, 
		TArrayView<const NNX::FMLTensorBinding> InputBindings,
		TArrayView<const NNX::FTensor> InputDesc,
		const NNX::FMLTensorBinding& OutputBinding,
		const NNX::FTensor& OutputDesc,
		bool OutputAsInput,
		EMLElementWiseVariadicOperatorType OpType,
		float Scale)
	{
		static_assert(FElementWiseVariadicConstants::MAX_NUM_INPUT == 4, "This algorithm need to be adapted to math the shader.");
		check(InputBindings.Num() == InputDesc.Num());
		check(InputBindings.Num() <= FElementWiseVariadicConstants::MAX_NUM_INPUT);
		check(InputBindings.Num() > 0);

		// SRVs & UAV creations
		FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBinding.Buffer, PF_R32_FLOAT));
		FRDGBufferSRVRef InputsSRV[FElementWiseVariadicConstants::MAX_NUM_INPUT] = { nullptr, nullptr, nullptr, nullptr };

		for (int32 i = 0; i < InputBindings.Num(); ++i)
		{
			InputsSRV[i] = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InputBindings[i].Buffer, PF_R32_FLOAT));
		}

		// Set parameters
		FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(OutputDesc.GetVolume(), FElementWiseVariadicConstants::NUM_GROUP_THREADS);
		TElementWiseVariadicCS::FParameters* Params = GraphBuilder.AllocParameters<TElementWiseVariadicCS::FParameters>();

		Params->Input0 = InputsSRV[0];
		Params->Input1 = InputsSRV[1];
		Params->Input2 = InputsSRV[2];
		Params->Input3 = InputsSRV[3];
		Params->Output = OutputUAV;
		FillTensorStrideForBroadcastShaderParameters(InputDesc[0], OutputDesc.GetShape().Rank(), Params->InputTensorInfo, 0);
		if (InputBindings.Num() >= 2)
		{
			FillTensorStrideForBroadcastShaderParameters(InputDesc[1], OutputDesc.GetShape().Rank(), Params->InputTensorInfo, 1);
		}
		if (InputBindings.Num() >= 3)
		{
			FillTensorStrideForBroadcastShaderParameters(InputDesc[2], OutputDesc.GetShape().Rank(), Params->InputTensorInfo, 2);
		}
		if (InputBindings.Num() >= 4)
		{
			FillTensorStrideForBroadcastShaderParameters(InputDesc[3], OutputDesc.GetShape().Rank(), Params->InputTensorInfo, 3);
		}
		FillTensorStrideShaderParameters(OutputDesc, Params->OutputTensorInfo, 0);
		Params->Num = OutputDesc.GetVolume();
		Params->ThreadCountX = ThreadGroupCount.X * FElementWiseVariadicConstants::NUM_GROUP_THREADS;
		Params->Scale = Scale;

		// Shader variation
		TElementWiseVariadicCS::FPermutationDomain PermutationVector;

		PermutationVector.Set<TElementWiseVariadicCS::FOperatorType>(OpType);
		PermutationVector.Set<TElementWiseVariadicCS::FApplyScale>(Scale != 1.0f);
		PermutationVector.Set<TElementWiseVariadicCS::FOutputAsInput>(OutputAsInput);
		PermutationVector.Set<TElementWiseVariadicCS::FNumInput>(InputBindings.Num());
		PermutationVector.Set<TElementWiseVariadicCS::FVariadicNumDimensions>(OutputDesc.GetShape().Rank());

		// Add the pass to RDG
		TShaderMapRef<TElementWiseVariadicCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("NNI.Operator.Hlsl.ElementWise.Variadic.Dispatch"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			Params,
			ThreadGroupCount);
	}

	/**
	 * Variadic Element-wise ML operator implementation
	 */
	template<EMLElementWiseVariadicOperatorType OpType>
	class TElementWiseVariadic : public NNX::FMLOperatorHlsl
	{
	public:

		TElementWiseVariadic() {}
		virtual ~TElementWiseVariadic() = default;

	private:

		TArray<NNX::FTensor> InputDescs;
		NNX::FTensor OutputDesc = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FTensor> InputTensors, TArrayView<const NNX::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() > 0);
			check(OutputTensors.Num() == 1);
		
			InputDescs.Append(InputTensors);
			OutputDesc = OutputTensors[0];

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			check(OutOutputBindings.Num() == 1);
			check(InInputBindings.Num() == InputDescs.Num());

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.ElementWise.Variadic");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorElementWiseVariadic);

			NNX::FMLTensorBinding PassInputBindings[FElementWiseVariadicConstants::MAX_NUM_INPUT];
			NNX::FTensor PassInputDescs[FElementWiseVariadicConstants::MAX_NUM_INPUT];
			for (int32 InputOffset = 0; InputOffset < InInputBindings.Num(); InputOffset += FElementWiseVariadicConstants::MAX_NUM_INPUT)
			{
				uint32 NumInputLeftToHandle = InInputBindings.Num() - InputOffset;
				uint32 NumInputForPass = FMath::Min(FElementWiseVariadicConstants::MAX_NUM_INPUT, NumInputLeftToHandle);
				bool bIsFirstPass = (InputOffset == 0);
				bool bIsLastPass = (NumInputLeftToHandle <= FElementWiseVariadicConstants::MAX_NUM_INPUT);
				float Scale = 1.0f;

				for (uint32 i = 0; i < NumInputForPass; ++i)
				{
					PassInputBindings[i] = InInputBindings[InputOffset + i];
					PassInputDescs[i] = InputDescs[InputOffset + i];
				}

				if (OpType == EMLElementWiseVariadicOperatorType::Mean && bIsLastPass)
				{
					Scale = 1.0f / InInputBindings.Num();
				}
			
				AddOneVariadicOpPass(GraphBuilder,
					MakeArrayView(PassInputBindings, NumInputForPass),
					MakeArrayView(PassInputDescs, NumInputForPass),
					OutOutputBindings[0],
					OutputDesc,
					!bIsFirstPass,
					OpType, Scale);
			}
		}
	};

	template<EMLElementWiseVariadicOperatorType OpType>
	NNX::FMLOperatorHlsl* CreateElementWiseVariadicOperator()
	{
		return new TElementWiseVariadic<OpType>();
	}

	bool RegisterElementWiseVariadicOperators(NNX::FMLOperatorRegistryHlsl& Registry)
	{
#define OP(Name) Registry.OpAdd(TEXT(#Name), CreateElementWiseVariadicOperator<EMLElementWiseVariadicOperatorType::Name>)
		OP(Max);
		OP(Min);
		OP(Mean);
		OP(Sum);
#undef OP

		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
