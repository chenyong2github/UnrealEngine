// Copyright Epic Games, Inc. All Rights Reserved.
#include "Modules/ModuleManager.h"

#include "NNECoreAttributeMap.h"
#include "NNXCore.h"
#include "NNXModelBuilder.h"
#include "NNXModelOptimizer.h"
#include "NNXModelOptimizerInterface.h"
#include "NNXRuntimeFormat.h"

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "HAL/IConsoleManager.h"

#include "Misc/FileHelper.h"

//
//
//
class FNNXUtilsModule : public IModuleInterface
{
public:

	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FNNXUtilsModule, NNXUtils)


using namespace NNX;

static FAutoConsoleCommand ConsoleCmdNNXOptimizeModel(
	TEXT("nnx.test.OptimizeModel"),
	TEXT("Optimize the model from file and store it into NNX format.Examples: OptimizeModel -f path.onnx, OptimizeModel -op Cos"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			// Parse arguments
			FString	ModelPath;
			FString OpName;

			for (int ArgIdx = 0; ArgIdx < Args.Num(); ++ArgIdx)
			{
				if (Args[ArgIdx] == TEXT("-f"))
				{
					if (ArgIdx + 1 < Args.Num())
					{
						ModelPath = Args[ArgIdx + 1];
						break;
					}
					else
					{
						UE_LOG(LogNNX, Warning, TEXT("Found option -f but file path is not provided"));
						return;
					}
				}
				else if (Args[ArgIdx] == TEXT("-op"))
				{
					if (ArgIdx + 1 < Args.Num())
					{
						OpName = Args[ArgIdx + 1];
						break;
					}
					else
					{
						UE_LOG(LogNNX, Warning, TEXT("Found option -op but operator name is not not provided"));
						return;
					}
				}

			}

			FNNIModelRaw ONNXModel;
			
			// First try to load model from file, otherwise create a single layer NN with specified
			// operator name
			if (!ModelPath.IsEmpty())
			{
				if (!FFileHelper::LoadFileToArray(ONNXModel.Data, *ModelPath))
				{
					UE_LOG(LogNNX, Warning, TEXT("Failed to load model from file:%s"), *ModelPath);
					return;
				}
				ONNXModel.Format = ENNXInferenceFormat::ONNX;
			}
			else if (!OpName.IsEmpty())
			{
				FTensor InputTensor = FTensor::Make(TEXT("in"), FTensorShape::Make({1, 512 }), EMLTensorDataType::Float);
				FTensor OutputTensor = FTensor::Make(TEXT("out"), FTensorShape::Make({ 1, 512 }), EMLTensorDataType::Float);

				if (!NNX::CreateONNXModelForOperator(OpName, MakeArrayView(&InputTensor, 1), MakeArrayView(&OutputTensor, 1), ONNXModel))
				{
					UE_LOG(LogNNX, Warning, TEXT("Failed to create model for operator:%s"), *OpName);
				}
			}
			else
			{
				UE_LOG(LogNNX, Warning, TEXT("Invalid arguments"));
			}

			TUniquePtr<IModelOptimizer> Optimizer = CreateONNXToNNXModelOptimizer();

			if (!Optimizer)
			{
				UE_LOG(LogNNX, Warning, TEXT("Failed to create instance of model optimizer"));
				return;
			}

			UE_LOG(LogNNX, Verbose, TEXT("Optimizing model"));

			FOptimizerOptionsMap Options;
			FNNIModelRaw OutModel;
			
			if (Optimizer->Optimize(ONNXModel, OutModel, Options))
			{
				UE_LOG(LogNNX, Display, TEXT("Model optimized:%u bytes"), OutModel.Data.Num());
			}
			else
			{
				UE_LOG(LogNNX, Warning, TEXT("Failed to optimize the model"));
			}
		}
	)
);

static FAutoConsoleCommand ConsoleCmdNNXCreateModel(
	TEXT("nnx.test.CreateONNX"),
	TEXT("Create a single layer ONNX model"),
	FConsoleCommandWithArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args)
		{
			using NNX::FTensor;

			if (Args.Num() > 0)
			{
				FNNIModelRaw ONNXModel;
				FTensor InputTensor = FTensor::Make(TEXT("in"), FTensorShape::Make({ 1, 512 }), EMLTensorDataType::Float);
				FTensor OutputTensor = FTensor::Make(TEXT("out"), FTensorShape::Make({ 1, 512 }), EMLTensorDataType::Float);

				if (!NNX::CreateONNXModelForOperator(Args[0], MakeArrayView(&InputTensor, 1), MakeArrayView(&OutputTensor, 1), ONNXModel))
				{
					UE_LOG(LogNNX, Display, TEXT("Failed to create model for operator:%s"), *Args[0]);
				}
			}
		}
	)
);