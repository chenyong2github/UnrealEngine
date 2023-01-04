// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralMorphNetwork.h"
#include "NeuralMorphModel.h"
#include "UObject/SoftObjectPath.h"
#include "HAL/FileManagerGeneric.h"
#include "Math/UnrealMathUtility.h"

#if NEURALMORPHMODEL_USE_ISPC
	#include "NeuralMorphNetwork.ispc.generated.h"
#endif

void UNeuralMorphNetwork::Empty()
{
	InputMeans.Empty();
	InputStd.Empty();
	Layers.Empty();
	Mode = ENeuralMorphMode::Global;
	NumMorphsPerBone = 0;
	NumBones = 0;
	NumCurves = 0;
}

bool UNeuralMorphNetwork::IsEmpty() const
{
	return GetNumInputs() == 0;
}

int32 UNeuralMorphNetwork::GetNumInputs() const
{
	return !Layers.IsEmpty() ? Layers[0]->NumInputs * Layers[0]->Depth : 0;
}

int32 UNeuralMorphNetwork::GetNumOutputs() const
{
	return !Layers.IsEmpty() ? Layers[Layers.Num() - 1]->NumOutputs * Layers[Layers.Num() - 1]->Depth : 0;
}

bool UNeuralMorphNetwork::Load(const FString& Filename)
{
	UE_LOG(LogNeuralMorphModel, Display, TEXT("Loading Neural Morph Network from file '%s'"), *Filename);
	Empty();

	// Create the file reader.
	TUniquePtr<FArchive> FileReader(IFileManager::Get().CreateFileReader(*Filename));
	if (!FileReader.IsValid())
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to create file reader to read file '%s'!"), *Filename);
		Empty();
		return false;
	}

	// Read the FOURCC, to identify the file type.
	char FOURCC[4] {' ', ' ', ' ', ' '};
	FileReader->Serialize(FOURCC, 4);
	if (FileReader->IsError())
	{
		FileReader->Close();
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to read the FOURCC!"));
		Empty();
		return false;
	}

	if (FOURCC[0] != 'N' || FOURCC[1] != 'M' || FOURCC[2] != 'M' || FOURCC[3] != 'N')	// NMMN (Neural Morph Model Network)
	{
		FileReader->Close();
		UE_LOG(LogNeuralMorphModel, Error, TEXT("The file is not a valid valid neural morph network file type!"));
		Empty();
		return false;
	}

	// Load the version number.
	int32 Version = -1;
	FileReader->Serialize(&Version, sizeof(int32));
	if (FileReader->IsError())
	{
		FileReader->Close();
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load version"));
		Empty();
		return false;
	}

	if (Version != 2)
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("The Neural Morph Network file '%s' is of an unknown version (Version=%d)!"), *Filename, Version);
		FileReader->Close();
		Empty();
		return false;
	}

	// Load the header with info.
	struct FInfoHeader
	{
		int32 Mode = -1;					// 0=Local, 1=Global
		int32 NumInputs = -1;				// How many float inputs in the input layer?
		int32 NumHiddenLayers = -1;			// How many hidden layers in the network (layers excluding input and output layer)?
		int32 NumUnitsPerHiddenLayer = -1;	// How many neurons for each hidden layer?
		int32 NumOutputs = -1;				// The number of units in the output layer.
		int32 NumMorphsPerBone = -1;		// The number of morph targets per bone, if set Mode == 0. Otherwise ignored.
		int32 NumBones = -1;				// The number of bones used as input.
		int32 NumCurves = -1;				// The number of curves used as input.
		int32 NumFloatsPerCurve = -1;		// The number of floats per curve.
	};

	FInfoHeader Info;
	FileReader->Serialize(&Info, sizeof(FInfoHeader));
	if (FileReader->IsError())
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load info header!"));
		FileReader->Close();
		Empty();
		return false;
	}

	if (Info.Mode == -1 || Info.NumInputs == -1 || Info.NumHiddenLayers == -1 || Info.NumUnitsPerHiddenLayer == -1 || Info.NumOutputs == -1 || Info.NumBones == -1 || Info.NumCurves == -1 || Info.NumFloatsPerCurve == -1)
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to read info header for Neural Morph Network in file '%s'!"), *FileReader->GetArchiveName());
		FileReader->Close();
		Empty();
		return false;
	}

	Mode = (Info.Mode == 0) ? ENeuralMorphMode::Local : ENeuralMorphMode::Global;
	NumMorphsPerBone = Info.NumMorphsPerBone;
	NumBones = Info.NumBones;
	NumCurves = Info.NumCurves;
	NumFloatsPerCurve = Info.NumFloatsPerCurve;

	// Read the input standard deviation and means.
	InputMeans.SetNumZeroed(Info.NumInputs);
	InputStd.SetNumZeroed(Info.NumInputs);
	FileReader->Serialize(InputStd.GetData(), Info.NumInputs * sizeof(float));
	if (FileReader->IsError())
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load input standard deviations!"));
		FileReader->Close();
		Empty();
		return false;
	}

	FileReader->Serialize(InputMeans.GetData(), Info.NumInputs * sizeof(float));
	if (FileReader->IsError())
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load input means!"));
		FileReader->Close();
		Empty();
		return false;
	}

	struct FLayerHeader
	{
		int32 NumInputs = -1;
		int32 NumOutputs = -1;
		int32 NumWeights = -1;
		int32 NumBiases = -1;
	};

	// Load the weights and biases.
	for (int32 LayerIndex = 0; LayerIndex < Info.NumHiddenLayers + 1; ++LayerIndex)
	{
		UNeuralMorphNetworkLayer* WeightData = NewObject<UNeuralMorphNetworkLayer>(this);

		FLayerHeader LayerHeader;
		FileReader->Serialize(&LayerHeader, sizeof(FLayerHeader));
		if (FileReader->IsError())
		{
			UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load layer header!"));
			FileReader->Close();
			Empty();
			return false;
		}

		WeightData->NumInputs = LayerHeader.NumInputs;
		WeightData->NumOutputs = LayerHeader.NumOutputs;
		WeightData->Depth = LayerHeader.NumWeights / (LayerHeader.NumInputs * LayerHeader.NumOutputs);

		WeightData->Weights.AddZeroed(LayerHeader.NumWeights);
		FileReader->Serialize(WeightData->Weights.GetData(), LayerHeader.NumWeights * sizeof(float));
		if (FileReader->IsError())
		{
			UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load layer weights!"));
			FileReader->Close();
			Empty();
			return false;
		}

		WeightData->Biases.AddZeroed(LayerHeader.NumBiases);
		FileReader->Serialize(WeightData->Biases.GetData(), LayerHeader.NumBiases * sizeof(float));
		if (FileReader->IsError())
		{
			UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to load layer biases!"));
			FileReader->Close();
			Empty();
			return false;
		}

		Layers.Add(WeightData);

		UE_LOG(LogNeuralMorphModel, Verbose, TEXT("Network Layer %d --> NumWeights=%d (%dx%dx%d)   NumBiases=%d"), LayerIndex, WeightData->Weights.Num(), WeightData->NumInputs, WeightData->NumOutputs, WeightData->Depth, WeightData->Biases.Num());
	}

	// Clean up and return the result.
	const bool bSuccess = FileReader->Close();
	if (bSuccess)
	{
		UE_LOG(LogNeuralMorphModel, Display, TEXT("Successfullly loaded neural morph network from file '%s'"), *FileReader->GetArchiveName());
	}
	else
	{
		UE_LOG(LogNeuralMorphModel, Error, TEXT("Failed to close file reader."));
		Empty();
	}

	return bSuccess;
}

UNeuralMorphNetworkInstance* UNeuralMorphNetwork::CreateInstance()
{
	UNeuralMorphNetworkInstance* Instance = NewObject<UNeuralMorphNetworkInstance>(this);
	Instance->Init(this);
	return Instance;
}

int32 UNeuralMorphNetwork::GetNumBones() const
{
	return NumBones;
}

int32 UNeuralMorphNetwork::GetNumCurves() const
{
	return NumCurves;
}

int32 UNeuralMorphNetwork::GetNumMorphsPerBone() const
{
	return NumMorphsPerBone;
}

ENeuralMorphMode UNeuralMorphNetwork::GetMode() const
{
	return Mode;
}

const TArrayView<const float> UNeuralMorphNetwork::GetInputMeans() const
{
	return InputMeans;
}

const TArrayView<const float> UNeuralMorphNetwork::GetInputStds() const
{
	return InputStd;
}

int32 UNeuralMorphNetwork::GetNumLayers() const
{
	return Layers.Num();
}

UNeuralMorphNetworkLayer& UNeuralMorphNetwork::GetLayer(int32 Index) const
{
	return *Layers[Index].Get();
}

int32 UNeuralMorphNetwork::GetNumFloatsPerCurve() const
{
	return NumFloatsPerCurve;
}

//--------------------------------------------------------------------------
// UNeuralMorphNetworkInstance
//--------------------------------------------------------------------------

TArrayView<float> UNeuralMorphNetworkInstance::GetInputs()
{ 
	return TArrayView<float>(Inputs.GetData(), Inputs.Num());
}

TArrayView<const float> UNeuralMorphNetworkInstance::GetInputs() const
{ 
	return TArrayView<const float>(Inputs.GetData(), Inputs.Num());
}

TArrayView<float> UNeuralMorphNetworkInstance::GetOutputs()
{ 
	return TArrayView<float>(Outputs.GetData(), Outputs.Num());
}

TArrayView<const float> UNeuralMorphNetworkInstance::GetOutputs() const
{ 
	return TArrayView<const float>(Outputs.GetData(), Outputs.Num());
}

const UNeuralMorphNetwork& UNeuralMorphNetworkInstance::GetNeuralNetwork() const
{ 
	return *Network.Get();
}

void UNeuralMorphNetworkInstance::Init(UNeuralMorphNetwork* InNeuralNetwork)
{
	Network = InNeuralNetwork;
	Inputs.SetNumZeroed(Network->GetNumInputs());
	Outputs.SetNumZeroed(Network->GetNumOutputs());

	// Find the largest layer unit size and pre-allocate a buffer of that size.
	int32 MaxNumUnits = 0;
	const int32 NumLayers = Network->GetNumLayers();
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		const UNeuralMorphNetworkLayer& CurLayer = Network->GetLayer(LayerIndex);
		const int32 NumInputUnits = CurLayer.NumInputs * CurLayer.Depth;
		const int32 NumOutputUnits = CurLayer.NumOutputs * CurLayer.Depth;
		MaxNumUnits = FMath::Max3<int32>(NumInputUnits, NumOutputUnits, MaxNumUnits);
	}

	TempInputArray.SetNumZeroed(MaxNumUnits);
	TempOutputArray.SetNumZeroed(MaxNumUnits);
}

void UNeuralMorphNetworkInstance::RunGlobalModel(const FRunSettings& RunSettings)
{
	float* RESTRICT TempInputBuffer = RunSettings.TempInputBuffer;
	float* RESTRICT TempOutputBuffer = RunSettings.TempOutputBuffer;

	checkfSlow(Network->GetNumFloatsPerCurve() == 1, TEXT("Expecting the number of floats per curve to be 1 in global mode."));

	const int32 NumLayers = Network->GetNumLayers();
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		const UNeuralMorphNetworkLayer& CurLayer = Network->GetLayer(LayerIndex);
		const int32 NumLayerInputs = CurLayer.NumInputs;
		const int32 NumLayerOutputs = CurLayer.NumOutputs;

		// Normalize inputs for the first layer inputs.
		if (LayerIndex == 0)
		{
			const float* const RESTRICT NetworkInputs = RunSettings.InputBuffer;
			const float* const RESTRICT Means = RunSettings.InputMeansBuffer;
			const float* const RESTRICT Stds = RunSettings.InputStdsBuffer;
			for (int32 Index = 0; Index < NumLayerInputs; ++Index)
			{
				TempInputBuffer[Index] = (NetworkInputs[Index] - Means[Index]) / Stds[Index];
			}
		}

		// If we reached the last layer, output to the final buffer.
		if (LayerIndex == NumLayers - 1)
		{
			TempOutputBuffer = RunSettings.OutputBuffer;
		}

		#if NEURALMORPHMODEL_USE_ISPC
			const float* const RESTRICT LayerWeights = CurLayer.Weights.GetData();
			const float* const RESTRICT LayerBiases = CurLayer.Biases.GetData();
			ispc::MorphNeuralNetwork_LayerForward(TempOutputBuffer, TempInputBuffer, LayerWeights, LayerBiases, NumLayerInputs, NumLayerOutputs);
		#else
			// Init the outputs to the bias.
			const float* const RESTRICT LayerBiases = CurLayer.Biases.GetData();
			for (int32 Index = 0; Index < NumLayerOutputs; ++Index)
			{
				TempOutputBuffer[Index] = LayerBiases[Index];
			}

			// Multiply layer inputs with the weights.
			const float* const RESTRICT LayerWeights = CurLayer.Weights.GetData();
			for (int32 InputIndex = 0; InputIndex < NumLayerInputs; ++InputIndex)
			{
				const float InputValue = TempInputBuffer[InputIndex];
				const int32 Offset = InputIndex * NumLayerOutputs;
				for (int32 OutputIndex = 0; OutputIndex < NumLayerOutputs; ++OutputIndex)
				{
					TempOutputBuffer[OutputIndex] += InputValue * LayerWeights[Offset + OutputIndex];
				}
			}

			// Apply ELU activation.
			for (int32 Index = 0; Index < NumLayerOutputs; ++Index)
			{
				const float X = TempOutputBuffer[Index];
				TempOutputBuffer[Index] = (X > 0.0f) ? X : FMath::InvExpApprox(-X) - 1.0f;
			}
		#endif

		// The outputs are now input to the next layer.
		if (LayerIndex < NumLayers - 1)
		{
			// Swap the inputs and outputs, as the outputs are the input to the next layer.
			float* const SwapTemp = TempInputBuffer;
			TempInputBuffer = TempOutputBuffer;
			TempOutputBuffer = SwapTemp;
		}
	}
}


void UNeuralMorphNetworkInstance::RunLocalModel(const FRunSettings& RunSettings)
{
	float* RESTRICT TempInputBuffer = RunSettings.TempInputBuffer;
	float* RESTRICT TempOutputBuffer = RunSettings.TempOutputBuffer;

	checkfSlow(Network->GetNumFloatsPerCurve() == 6, TEXT("Expecting num floats per curve to be 6 in local mode."));

	const int32 NumLayers = Network->GetNumLayers();
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		const UNeuralMorphNetworkLayer& CurLayer = Network->GetLayer(LayerIndex);
		const int32 NumInputsPerBlock = CurLayer.NumInputs;
		checkSlow(NumInputsPerBlock == 6);
		const int32 NumBlocks = CurLayer.Depth;

		// Normalize inputs for the first layer inputs.
		if (LayerIndex == 0)
		{
			const float* const RESTRICT NetworkInputs = RunSettings.InputBuffer;
			const float* const RESTRICT Means = RunSettings.InputMeansBuffer;
			const float* const RESTRICT Stds = RunSettings.InputStdsBuffer;
			const int32 NumInputs = CurLayer.NumInputs * CurLayer.Depth;
			for (int32 Index = 0; Index < NumInputs; ++Index)
			{
				TempInputBuffer[Index] = (NetworkInputs[Index] - Means[Index]) / Stds[Index];
			}
		}

		// If we reached the last layer, output to the final buffer.
		if (LayerIndex == NumLayers - 1)
		{
			TempOutputBuffer = RunSettings.OutputBuffer;
		}

		// Init the output buffer to the bias values.
		const float* const RESTRICT LayerBiases = CurLayer.Biases.GetData();
		const int32 NumOutputsPerBlock = CurLayer.NumOutputs;
		const int32 NumOutputs = CurLayer.NumOutputs * CurLayer.Depth;
		for (int32 Index = 0; Index < NumOutputs; ++Index)
		{
			TempOutputBuffer[Index] = LayerBiases[Index];
		}

		const float* const RESTRICT LayerWeights = CurLayer.Weights.GetData();
		for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
		{
			const int32 BlockOutputOffset = BlockIndex * NumOutputsPerBlock;
			const int32 BlockInputOffset = BlockIndex * NumInputsPerBlock;

			// Multiply layer inputs with the weights.
			const int32 WeightOffset = BlockIndex * (NumInputsPerBlock * NumOutputsPerBlock);
			for (int32 InputIndex = 0; InputIndex < NumInputsPerBlock; ++InputIndex)
			{
				const float InputValue = TempInputBuffer[BlockInputOffset + InputIndex];
				const int32 InputOffset = InputIndex * NumOutputsPerBlock;
				for (int32 OutputIndex = 0; OutputIndex < NumOutputsPerBlock; ++OutputIndex)
				{
					TempOutputBuffer[BlockOutputOffset + OutputIndex] += InputValue * LayerWeights[WeightOffset + InputOffset + OutputIndex];
				}
			}
		} // For all blocks.

		// Apply ELU activation.
		#if NEURALMORPHMODEL_USE_ISPC
			ispc::MorphNeuralNetwork_Activation_ELU(TempOutputBuffer, NumOutputs);
		#else
			for (int32 OutputIndex = 0; OutputIndex < NumOutputs; ++OutputIndex)
			{
				const float X = TempOutputBuffer[OutputIndex];
				TempOutputBuffer[OutputIndex] = (X > 0.0f) ? X : FMath::InvExpApprox(-X) - 1.0f;
			}
		#endif

		// The outputs are now input to the next layer.
		if (LayerIndex < NumLayers - 1)
		{
			// Swap the inputs and outputs, as the outputs are the input to the next layer.
			float* const SwapTemp = TempInputBuffer;
			TempInputBuffer = TempOutputBuffer;
			TempOutputBuffer = SwapTemp;
		}
	} // For all layers.
}


void UNeuralMorphNetworkInstance::Run()
{	
	TRACE_CPUPROFILER_EVENT_SCOPE(UNeuralMorphNetwork::Run)

	// Setup the buffer pointers.
	FRunSettings RunSettings;
	RunSettings.TempInputBuffer  = TempInputArray.GetData();
	RunSettings.TempOutputBuffer = TempOutputArray.GetData();
	RunSettings.InputBuffer		 = Inputs.GetData();
	RunSettings.InputMeansBuffer = Network->GetInputMeans().GetData();
	RunSettings.InputStdsBuffer  = Network->GetInputStds().GetData();
	RunSettings.OutputBuffer	 = Outputs.GetData();

	// Run the network.
	if (Network->GetMode() == ENeuralMorphMode::Global)
	{
		RunGlobalModel(RunSettings);
	}
	else
	{
		checkSlow(Network->GetMode() == ENeuralMorphMode::Local)
		RunLocalModel(RunSettings);
	}
}
