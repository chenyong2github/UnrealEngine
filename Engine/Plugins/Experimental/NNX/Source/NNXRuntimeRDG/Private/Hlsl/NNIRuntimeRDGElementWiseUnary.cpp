// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGElementWiseUnary.h"
#include "NNEHlslShadersElementWiseUnaryCS.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNECoreAttributeMap.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorElementWiseUnary, TEXT("NNI.Operator.Hlsl.ElementWise.Unary"));

	using TElementWiseUnaryCS = typename UE::NNEHlslShaders::Internal::TElementWiseUnaryCS;
	using FElementWiseUnaryConstants = UE::NNEHlslShaders::Internal::FElementWiseUnaryConstants;

	/**
	 * Unary element-wise operator implementation
	 */
	template<EMLElementWiseUnaryOperatorType OpType>
	class TElementWiseUnary : public NNX::FMLOperatorHlsl
	{
	public:

		TElementWiseUnary() {}
		virtual ~TElementWiseUnary() = default;

	private:

		float Alpha = 0.0f;
		float Beta = 0.0f;
		float Gamma = 0.0f;
		NNX::FMLTensorDesc Input = {};
		NNX::FMLTensorDesc Output = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FMLTensorDesc> InputTensors, TArrayView<const NNX::FMLTensorDesc> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() == 1);
			check(OutputTensors.Num() == 1);

			Input = InputTensors[0];
			Output = OutputTensors[0];

			Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
			Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
			Gamma = Attributes.GetValueOrDefault(TEXT("gamma"), Gamma);

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
            //struct RDGTensor
            //{
            //    RDGBuffer*
            //    TensorShape ? Do we need the type? aka TensorDesc?
            //}

			//Dispatch(FRDGBuilder & GraphBuilder, 
			         //TConstArrayView<const NNX::RDGTensor> InputBuffers,
					 //TConstArrayView<const NNX::RDGTensor> OutputBuffers) override

            //or 
            //Dispatch(FRDGBuilder & GraphBuilder, 
                     //TConstArrayView<const NNX::FConcreteShape> InputShapes,
			         //TConstArrayView<const NNX::RDGTensor*> InputBuffers,
                     //TConstArrayView<const NNX::FConcreteShape> OutputShapes,
					 //TConstArrayView<const NNX::RDGTensor*> OutputBuffers) override

            //or 
            //Dispatch(FRDGBuilder & GraphBuilder, 
                     //TConstArrayView<const NNX::RDGTensor*> Buffers,
                     //TConstArrayView<const NNX::FConcreteShape> Shapes,
			         //TConstArrayView<const uint32> InputIndices,
                     //TConstArrayView<const uint32> OutputIndices) override

			// HACK: This only works for single layer networks
			FRDGBufferSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));
		
			FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(Output.Volume, FElementWiseUnaryConstants::NUM_GROUP_THREADS);

			// Set parameters
			TElementWiseUnaryCS::FParameters* Params = GraphBuilder.AllocParameters<TElementWiseUnaryCS::FParameters>();
			Params->Input = InputSRV;
			Params->Output = OutputUAV;
			Params->Alpha = Alpha;
			Params->Beta = Beta;
			Params->Gamma = Gamma;
			Params->Num = Output.Volume;
			Params->ThreadCountX = ThreadGroupCount.X * FElementWiseUnaryConstants::NUM_GROUP_THREADS;

			TElementWiseUnaryCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<TElementWiseUnaryCS::FOperatorType>(OpType);

			TShaderMapRef<TElementWiseUnaryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.ElementWise.Unary");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorElementWiseUnary);
		
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.ElementWise.Unary.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				ThreadGroupCount);
		}
	};

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::Selu>::TElementWiseUnary()
		: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f)
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::Elu>::TElementWiseUnary()
		: Alpha(1.0f), Beta(0.0f), Gamma(0.0f) 
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::HardSigmoid>::TElementWiseUnary()
		: Alpha(0.2f), Beta(0.5f), Gamma(0.0f)
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::LeakyRelu>::TElementWiseUnary()
		: Alpha(0.01f), Beta(0.0f), Gamma(0.0f)
	{
	}

	template<EMLElementWiseUnaryOperatorType OpType>
	NNX::FMLOperatorHlsl* CreateElementWiseUnaryOperator()
	{
		return new TElementWiseUnary<OpType>();
	}

	bool RegisterElementWiseUnaryOperators(NNX::FMLOperatorRegistryHlsl& Registry)
	{
#define OP(Name) Registry.OpAdd(TEXT(#Name), CreateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::Name>)
		OP(Abs);
		OP(Acos);
		OP(Acosh);
		OP(Asin);
		OP(Asinh);
		OP(Atan);
		OP(Atanh);
		//OP(BitShift);
		//OP(Cast);
		OP(Ceil);
		//OP(Clip);
		OP(Cos);
		OP(Cosh);
		OP(Elu);
		OP(Erf);
		OP(Exp);
		OP(Floor);
		OP(IsInf);
		OP(IsNan);
		OP(HardSigmoid);
		OP(HardSwish);
		OP(LeakyRelu);
		OP(Log);
		OP(Neg);
		//OP(Not);
		OP(Reciprocal);
		OP(Relu);
		OP(Round);
		OP(Selu);
		OP(Sigmoid);
		OP(Sign);
		OP(Sin);
		OP(Sinh);
		OP(Softplus);
		OP(Softsign);
		OP(Sqrt);
		OP(Tan);
		OP(Tanh);
#undef OP

		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
