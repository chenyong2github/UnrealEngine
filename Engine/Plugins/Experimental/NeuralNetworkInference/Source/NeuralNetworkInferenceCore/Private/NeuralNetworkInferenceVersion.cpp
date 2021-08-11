// Copyright Epic Games, Inc. All Rights Reserved.

#include "NeuralNetworkInferenceVersion.h"
#include "NeuralNetworkInferenceCoreUtils.h"



/* FNeuralNetworkInferenceVersion members and functions
 *****************************************************************************/

const int32 FNeuralNetworkInferenceVersion::VERSION_MAJOR(0);
const int32 FNeuralNetworkInferenceVersion::VERSION_MIDDLE(2);
const int32 FNeuralNetworkInferenceVersion::VERSION_MINOR(0);

bool FNeuralNetworkInferenceVersion::CheckVersion(const TArray<int32>& InVersion)
{
	// Sanity checks
	if (InVersion.Num() != 3)
	{
		UE_LOG(LogNeuralNetworkInferenceCore, Warning, TEXT("FNeuralNetworkInferenceVersion::CheckVersion(): Load() was never called on this class."
			" The model must be loaded or reimported from its original ONNX file."),
			FNeuralNetworkInferenceVersion::VERSION_MAJOR, FNeuralNetworkInferenceVersion::VERSION_MIDDLE, FNeuralNetworkInferenceVersion::VERSION_MINOR);
		return false;
	}
	else if (InVersion[0] != FNeuralNetworkInferenceVersion::VERSION_MAJOR || InVersion[1] != FNeuralNetworkInferenceVersion::VERSION_MIDDLE)
	{
		UE_LOG(LogNeuralNetworkInferenceCore, Warning, TEXT("FNeuralNetworkInferenceVersion::CheckVersion(): This class was saved with an old and deprecated format (version"
			" %d.%d.%d, current version: %d.%d.%d). The network must be reimported from its original ONNX file."), InVersion[0], InVersion[1], InVersion[2],
			FNeuralNetworkInferenceVersion::VERSION_MAJOR, FNeuralNetworkInferenceVersion::VERSION_MIDDLE, FNeuralNetworkInferenceVersion::VERSION_MINOR);
		return false;
	}
	else if (InVersion[2] != FNeuralNetworkInferenceVersion::VERSION_MINOR)
	{
		UE_LOG(LogNeuralNetworkInferenceCore, Display, TEXT("FNeuralNetworkInferenceVersion::CheckVersion(): Class saved with an older format (version"
			" %d.%d.%d, current version: %d.%d.%d). Reimporting the model from its original ONNX file might improve its performance."), InVersion[0], InVersion[1], InVersion[2],
			FNeuralNetworkInferenceVersion::VERSION_MAJOR, FNeuralNetworkInferenceVersion::VERSION_MIDDLE, FNeuralNetworkInferenceVersion::VERSION_MINOR);
	}
	return true;
}

TArray<int32> FNeuralNetworkInferenceVersion::GetVersion()
{
	return TArray<int32>({ FNeuralNetworkInferenceVersion::VERSION_MAJOR, FNeuralNetworkInferenceVersion::VERSION_MIDDLE, FNeuralNetworkInferenceVersion::VERSION_MINOR });
}
