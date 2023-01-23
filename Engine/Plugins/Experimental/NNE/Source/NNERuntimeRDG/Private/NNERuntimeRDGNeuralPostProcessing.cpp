// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGNeuralPostProcessing.h"
#include "NNXCore.h"
#include "PostProcess/PostProcessMaterial.h"
#include "PostProcess/SceneRenderTargets.h"
#include "ScreenPass.h"
#include "PostProcess/PostProcessing.h"
#include "PixelShaderUtils.h"

#include "NNEHlslShadersNeuralPostProcessingCS.h"

DECLARE_GPU_STAT_NAMED(FNNENeuralPostProcessingReadInput, TEXT("NNE.NeuralPostProcessing.ReadInput"));
DECLARE_GPU_STAT_NAMED(FNNENeuralPostProcessingPreStep, TEXT("NNE.NeuralPostProcessing.PreStep"));
DECLARE_GPU_STAT_NAMED(FNNENeuralPostProcessingPostStep, TEXT("NNE.NeuralPostProcessing.PostStep"));
DECLARE_GPU_STAT_NAMED(FNNENeuralPostProcessingWriteOutput, TEXT("NNE.NeuralPostProcessing.WriteOutput"));

FNNENeuralPostProcessing::FNNENeuralPostProcessing(const FAutoRegister& AutoRegister) : FSceneViewExtensionBase(AutoRegister) 
{
	NumEnabled = 0;
	LastId = 0;
}

int32 FNNENeuralPostProcessing::Add(FString RuntimeName, UNNEModelData* ModelData)
{
	FScopeLock Lock(&CriticalSection);

	// Create the model
	NNX::IRuntime* Runtime = NNX::GetRuntime(RuntimeName);
	if (!Runtime)
	{
		UE_LOG(LogNNX, Error, TEXT("FNNENeuralPostProcessing: No runtime '%s' found. Valid runtimes are: "), *RuntimeName);
		TArray<NNX::IRuntime*> Runtimes = NNX::GetAllRuntimes();
		for (int32 i = 0; i < Runtimes.Num(); i++)
		{
			UE_LOG(LogNNX, Error, TEXT("- %s"), *Runtimes[i]->GetRuntimeName());
		}
		return -1;
	}

	if (!ModelData)
	{
		UE_LOG(LogNNX, Error, TEXT("FNNENeuralPostProcessing: Valid model data required to load the model"));
		return -1;
	}

	TConstArrayView<uint8> Data = ModelData->GetModelData(RuntimeName);
	if (Data.Num() < 1)
	{
		UE_LOG(LogNNX, Error, TEXT("FNNENeuralPostProcessing: No model data for %s found"), *RuntimeName);
		return -1;
	}

	TSharedPtr<NNX::FMLInferenceModel> Model = TSharedPtr<NNX::FMLInferenceModel>(Runtime->CreateModel(Data).Release());
	if (!Model.IsValid())
	{
		UE_LOG(LogNNX, Error, TEXT("FNNENeuralPostProcessing: Could not create model using %s"), *RuntimeName);
		return -1;
	}

	// Create a new id
	LastId = (LastId + 1) > 0 ? (LastId + 1) : 1;

	// Add the model to the map
	Models.Add(LastId, Model);
	SetWeight(LastId, 0.0);
	SetRangeScale(LastId, 1.0);
	SetInputSize(LastId, FIntPoint(-1, -1));

	return LastId;
}

bool FNNENeuralPostProcessing::Remove(int32 ModelId)
{
	FScopeLock Lock(&CriticalSection);

	bool bResult = Models.Find(ModelId) != nullptr;

	Disable(ModelId);
	Models.Remove(ModelId);
	Weights.Remove(ModelId);
	RangeScales.Remove(ModelId);
	InputSizes.Remove(ModelId);

	return bResult;
}

bool FNNENeuralPostProcessing::SetWeight(int32 ModelId, float Weight)
{
	FScopeLock Lock(&CriticalSection);

	if (Models.Find(ModelId) != nullptr)
	{
		Weights.Add(ModelId, Weight);
		return true;
	}
	return false;
}

bool FNNENeuralPostProcessing::SetRangeScale(int32 ModelId, float RangeScale)
{
	FScopeLock Lock(&CriticalSection);

	if (Models.Find(ModelId) != nullptr)
	{
		RangeScales.Add(ModelId, RangeScale);
		return true;
	}
	return false;
}

bool FNNENeuralPostProcessing::SetInputSize(int32 ModelId, FIntPoint InputSize)
{
	FScopeLock Lock(&CriticalSection);

	if (Models.Find(ModelId) != nullptr)
	{
		InputSizes.Add(ModelId, InputSize);
		return true;
	}
	return false;
}

void FNNENeuralPostProcessing::Enable(int32 ModelId)
{
	FScopeLock Lock(&CriticalSection);

	if (Enabled.Find(ModelId) == nullptr)
	{
		Enabled.Add(ModelId);
		NumEnabled++;
	}
}

void FNNENeuralPostProcessing::Disable(int32 ModelId)
{
	FScopeLock Lock(&CriticalSection);

	if (Enabled.Find(ModelId) != nullptr)
	{
		Enabled.Remove(ModelId);
		NumEnabled--;
	}
}

void FNNENeuralPostProcessing::PrePostProcessPass_RenderThread(FRDGBuilder& GraphBuilder, const FSceneView& View, const FPostProcessingInputs& Inputs)
{
	using namespace UE::NNEHlslShaders::Internal;
	const float WeightEpsilon = 1.0/65536.0;

	check(IsInRenderingThread());
	check(View.bIsViewInfo);

	float InputWeight = 0.0;
	{
		FScopeLock Lock(&CriticalSection);

		if (NumEnabled < 1)
		{
			return;
		}

		float WeightSum = 0.0;
		for (TPair<int32, float> Pair : Weights)
		{
			if (Enabled.Find(Pair.Key) != nullptr)
			{
				if (Pair.Value >= 0.0)
				{
					WeightSum += Pair.Value;
				}
				else
				{
					Weights.Add(Pair.Key, 0.0);
				}
			}
		}

		if (WeightSum > 1.0)
		{
			for (TPair<int32, float> Pair : Weights)
			{
				if (Enabled.Find(Pair.Key) != nullptr)
				{
					Weights.Add(Pair.Key, Pair.Value / WeightSum);
				}
			}
		}
		else
		{
			InputWeight = 1.0 - WeightSum;
		}
	}

	const FIntRect Viewport = static_cast<const FViewInfo&>(View).ViewRect; 
	FScreenPassTexture SceneColor((*Inputs.SceneTextures)->SceneColorTexture, Viewport);
	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
	FIntPoint TextureSize = (*Inputs.SceneTextures)->SceneColorTexture->Desc.Extent;

	// Read the input into an accumulation buffer 
	FRDGBufferDesc AccumulationBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), TextureSize.X * TextureSize.Y * 3);
	FRDGBufferRef AccumulationBuffer = GraphBuilder.CreateBuffer(AccumulationBufferDesc, *FString("NNENeuralPostProcessing::AccumulationBuffer"));
	FRDGBufferUAVRef AccumulationBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(AccumulationBuffer, PF_R32_FLOAT));

	bool bOverwrite = true;
	if (InputWeight > WeightEpsilon)
	{
		TNeuralPostProcessingReadInputCS::FParameters* ReadInputParameters = GraphBuilder.AllocParameters<TNeuralPostProcessingReadInputCS::FParameters>();
		ReadInputParameters->InputTexture = (*Inputs.SceneTextures)->SceneColorTexture;
		ReadInputParameters->InputTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		ReadInputParameters->InputTextureWidth = TextureSize.X;
		ReadInputParameters->InputTextureHeight = TextureSize.Y;
		ReadInputParameters->AccumulationBuffer = AccumulationBufferUAV;
		ReadInputParameters->Weight = InputWeight;

		FIntVector ReadInputThreadGroupCount = FIntVector(FMath::DivideAndRoundUp(TextureSize.X, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(TextureSize.Y, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), 1);
		TShaderMapRef<TNeuralPostProcessingReadInputCS> ReadInputShader(GlobalShaderMap);

		RDG_EVENT_SCOPE(GraphBuilder, "NNE.NeuralPostProcessing.ReadInput");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FNNENeuralPostProcessingReadInput);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("NNE.NeuralPostProcessing.ReadInput"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ReadInputShader,
			ReadInputParameters,
			ReadInputThreadGroupCount);

		bOverwrite = false;
	}

	// Process each network
	{
		FScopeLock Lock(&CriticalSection);
		
		for (TPair<int32, TSharedPtr<NNX::FMLInferenceModel, ESPMode::ThreadSafe>> Pair : Models)
		{
			if (Enabled.Find(Pair.Key) != nullptr && Weights.Find(Pair.Key) != nullptr)
			{
				float OutputWeight = *Weights.Find(Pair.Key);
				if (OutputWeight > WeightEpsilon)
				{
					float RangeScale = 1.0;
					if (RangeScales.Find(Pair.Key))
					{
						RangeScale = *RangeScales.Find(Pair.Key);
					}

					FIntPoint InputSize = FIntPoint(-1, -1);
					if (InputSizes.Find(Pair.Key))
					{
						InputSize = *InputSizes.Find(Pair.Key);
					}

					UE::NNECore::FSymbolicTensorShape InputShape = Pair.Value->GetInputTensorDescs()[0].GetShape();

					checkf(InputShape.Rank() == 4, TEXT("Neural Post Processing requires models with input shape 1 x 3 x height x width!"))
					checkf(InputShape.GetData()[0] == 1, TEXT("Neural Post Processing requires models with input shape 1 x 3 x height x width!"))
					checkf(InputShape.GetData()[1] == 3, TEXT("Neural Post Processing requires models with input shape 1 x 3 x height x width!"))

					int32 NeuralNetworkInputWidth = InputShape.GetData()[3] >= 0 ? InputShape.GetData()[3] : (InputSize.X >= 0 ? InputSize.X : TextureSize.X);
					int32 NeuralNetworkInputHeight = InputShape.GetData()[2] >= 0 ? InputShape.GetData()[2] : (InputSize.Y >= 0 ? InputSize.Y : TextureSize.Y);

					FRDGBufferDesc InputBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), NeuralNetworkInputWidth * NeuralNetworkInputHeight * 3);
					FRDGBufferRef InputBuffer = GraphBuilder.CreateBuffer(InputBufferDesc, *(FString("NNENeuralPostProcessing::NeuralNetowrkInput_") + FString::FromInt(Pair.Key)));
					FRDGBufferUAVRef InputBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(InputBuffer, PF_R32_FLOAT));

					TNeuralPostProcessingPreStepCS::FParameters* PreStepParameters = GraphBuilder.AllocParameters<TNeuralPostProcessingPreStepCS::FParameters>();
					PreStepParameters->InputTexture = (*Inputs.SceneTextures)->SceneColorTexture;
					PreStepParameters->InputTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
					PreStepParameters->InputTextureWidth = TextureSize.X;
					PreStepParameters->InputTextureHeight = TextureSize.Y;
					PreStepParameters->InputBufferWidth = NeuralNetworkInputWidth;
					PreStepParameters->InputBufferHeight = NeuralNetworkInputHeight;
					PreStepParameters->InputBuffer = InputBufferUAV;
					PreStepParameters->RangeScale = RangeScale;

					FIntVector PreStepThreadGroupCount = FIntVector(FMath::DivideAndRoundUp(NeuralNetworkInputWidth, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(NeuralNetworkInputHeight, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), 1);
					TShaderMapRef<TNeuralPostProcessingPreStepCS> PreStepShader(GlobalShaderMap);

					RDG_EVENT_SCOPE(GraphBuilder, "NNE.NeuralPostProcessing.PreStep");
					RDG_GPU_STAT_SCOPE(GraphBuilder, FNNENeuralPostProcessingPreStep);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("NNE.NeuralPostProcessing.PreStep"),
						ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
						PreStepShader,
						PreStepParameters,
						PreStepThreadGroupCount);

					TArray<NNX::FTensorShape> InputShapes;
					InputShapes.Add(NNX::FTensorShape::Make({ 1, 3, (uint32)NeuralNetworkInputHeight, (uint32)NeuralNetworkInputWidth }));

					Pair.Value->SetInputTensorShapes(InputShapes);
					NNX::FTensorShape OutputShape = Pair.Value->GetOutputTensorShapes()[0];

					checkf(OutputShape.Rank() == 4, TEXT("Neural Post Processing requires models with output shape 1 x 3 x height x width!"))
					checkf(OutputShape.GetData()[0] == 1, TEXT("Neural Post Processing requires models with output shape 1 x 3 x height x width!"))
					checkf(OutputShape.GetData()[1] == 3, TEXT("Neural Post Processing requires models with output shape 1 x 3 x height x width!"))
					checkf(OutputShape.GetData()[2] > 0, TEXT("Neural Post Processing requires models with output height > 0!"))
					checkf(OutputShape.GetData()[3] > 0, TEXT("Neural Post Processing requires models with output width > 0!"))

					int32 NeuralNetworkOutputWidth = OutputShape.GetData()[3];
					int32 NeuralNetworkOutputHeight = OutputShape.GetData()[2];

					FRDGBufferDesc OutputBufferDesc = FRDGBufferDesc::CreateBufferDesc(sizeof(float), NeuralNetworkOutputWidth * NeuralNetworkOutputHeight * 3);
					FRDGBufferRef OutputBuffer = GraphBuilder.CreateBuffer(OutputBufferDesc, *(FString("NNENeuralPostProcessing::NeuralNetowrkOutput_") + FString::FromInt(Pair.Key)));
					FRDGBufferUAVRef OutputBufferUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutputBuffer, PF_R32_FLOAT));

					TArray<NNX::FMLTensorBinding> InputBindings;
					InputBindings.Add(NNX::FMLTensorBinding::FromRDG(InputBuffer, InputBufferDesc.NumElements * InputBufferDesc.BytesPerElement, 0));
					TArray<NNX::FMLTensorBinding> OutputBindings;
					OutputBindings.Add(NNX::FMLTensorBinding::FromRDG(OutputBuffer, OutputBufferDesc.NumElements * OutputBufferDesc.BytesPerElement, 0));
					Pair.Value->EnqueueRDG(GraphBuilder, InputBindings, OutputBindings);

					TNeuralPostProcessingPostStepCS::FParameters* PostStepParameters = GraphBuilder.AllocParameters<TNeuralPostProcessingPostStepCS::FParameters>();
					PostStepParameters->OutputBufferWidth = NeuralNetworkOutputWidth;
					PostStepParameters->OutputBufferHeight = NeuralNetworkOutputHeight;
					PostStepParameters->OutputBuffer = OutputBufferUAV;
					PostStepParameters->InputTextureWidth = TextureSize.X;
					PostStepParameters->InputTextureHeight = TextureSize.Y;
					PostStepParameters->AccumulationBuffer = AccumulationBufferUAV;
					PostStepParameters->Weight = OutputWeight;
					PostStepParameters->RangeScale = RangeScale;

					TNeuralPostProcessingPostStepCS::FPermutationDomain PermutationVector;
					PermutationVector.Set<TNeuralPostProcessingPostStepCS::FNeuralPostProcessingOverwrite>(bOverwrite ? ENeuralPostProcessingOverwrite::Yes : ENeuralPostProcessingOverwrite::No);
					PermutationVector.Set<TNeuralPostProcessingPostStepCS::FNeuralPostProcessingInterpolate>((NeuralNetworkOutputWidth == TextureSize.X && NeuralNetworkOutputHeight == TextureSize.Y) ? ENeuralPostProcessingInterpolate::No : ENeuralPostProcessingInterpolate::Yes);

					FIntVector PostStepThreadGroupCount = FIntVector(FMath::DivideAndRoundUp(TextureSize.X, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), FMath::DivideAndRoundUp(TextureSize.Y, FNeuralPostProcessingConstants::THREAD_GROUP_SIZE), 1);
					TShaderMapRef<TNeuralPostProcessingPostStepCS> PostStepShader(GlobalShaderMap, PermutationVector);

					RDG_EVENT_SCOPE(GraphBuilder, "NNE.NeuralPostProcessing.PostStep");
					RDG_GPU_STAT_SCOPE(GraphBuilder, FNNENeuralPostProcessingPostStep);

					FComputeShaderUtils::AddPass(
						GraphBuilder,
						RDG_EVENT_NAME("NNE.NeuralPostProcessing.PostStep"),
						ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
						PostStepShader,
						PostStepParameters,
						PostStepThreadGroupCount);

					bOverwrite = false;
				}
			}
		}
	}

	// Write the result
	TNeuralPostProcessingWriteOutputPS::FParameters* WriteOutputParameters = GraphBuilder.AllocParameters<TNeuralPostProcessingWriteOutputPS::FParameters>();
	WriteOutputParameters->AccumulationBuffer = AccumulationBufferUAV;
	WriteOutputParameters->InputTextureWidth = TextureSize.X;
	WriteOutputParameters->InputTextureHeight = TextureSize.Y;
	WriteOutputParameters->RenderTargets[0] = FRenderTargetBinding(SceneColor.Texture, ERenderTargetLoadAction::ENoAction);

	RDG_EVENT_SCOPE(GraphBuilder, "NNE.NeuralPostProcessing.WriteOutput");
	RDG_GPU_STAT_SCOPE(GraphBuilder, FNNENeuralPostProcessingWriteOutput);

	TShaderMapRef<TNeuralPostProcessingWriteOutputPS> WriteOutputShader(GlobalShaderMap);
	FPixelShaderUtils::AddFullscreenPass(
		GraphBuilder,
		GlobalShaderMap,
		RDG_EVENT_NAME("NNE.NeuralPostProcessing.WriteOutput"),
		WriteOutputShader,
		WriteOutputParameters,
		Viewport);
}

int32 UNNENeuralPostProcessing::Add(FString RuntimeName, UNNEModelData* ModelData)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	return NeuralPostProcessing->Add(RuntimeName, ModelData);
}

bool UNNENeuralPostProcessing::Remove(int32 ModelId)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	return NeuralPostProcessing->Remove(ModelId);
}

bool UNNENeuralPostProcessing::SetWeight(int32 ModelId, float Weight)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	return NeuralPostProcessing->SetWeight(ModelId, Weight);
}

bool UNNENeuralPostProcessing::SetRangeScale(int32 ModelId, float RangeScale)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	return NeuralPostProcessing->SetRangeScale(ModelId, RangeScale);
}

bool UNNENeuralPostProcessing::SetInputSize(int32 ModelId, FIntPoint InputSize)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	return NeuralPostProcessing->SetInputSize(ModelId, InputSize);
}

void UNNENeuralPostProcessing::Enable(int32 ModelId)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	NeuralPostProcessing->Enable(ModelId);
}

void UNNENeuralPostProcessing::Disable(int32 ModelId)
{
	if (!NeuralPostProcessing.IsValid())
	{
		NeuralPostProcessing = FSceneViewExtensions::NewExtension<FNNENeuralPostProcessing>();
	}
	NeuralPostProcessing->Disable(ModelId);
}