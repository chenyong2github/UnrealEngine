// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelProto.h"
#include "ModelProtoFileReaderUtils.h"

// Protobuf includes
#ifdef WITH_PROTOBUF
#include "ThirdPartyWarningDisabler.h"
NNI_THIRD_PARTY_INCLUDES_START
#undef TEXT
#undef check
#include "onnx.proto3.pb.h"
NNI_THIRD_PARTY_INCLUDES_END
#endif //WITH_PROTOBUF

class FModelProtoConverter
{
public:
	/**
	 * It creates and fills OutModelProto from the *.onnx file read into InIfstream.
	 * @return False if it could not initialize it, true if successful.
	 */
	static bool ConvertFromONNXProto3Ifstream(FModelProto& OutModelProto, std::istream& InIfstream);

#ifdef WITH_PROTOBUF
private:
	static bool ConvertProto3ToUAsset(FModelProto& OutModelProto, const onnx::ModelProto& InONNXModelProto);

	static bool ConvertProto3ToUAsset(FOperatorSetIdProto& OutOperatorSetIdProto, const onnx::OperatorSetIdProto& InONNXOperatorSetIdProto);

	static bool ConvertProto3ToUAsset(FTrainingInfoProto& OutTrainingInfoProto, const onnx::TrainingInfoProto& InONNXTrainingInfoProto);

	static bool ConvertProto3ToUAsset(FGraphProto& OutGraphProto, const onnx::GraphProto& InONNXGraphProto);

	static bool ConvertProto3ToUAsset(FNodeProto& OutNodeProto, const onnx::NodeProto& InONNXNodeProto);

	static bool ConvertProto3ToUAsset(FTensorAnnotation& OutTensorAnnotation, const onnx::TensorAnnotation& InONNXTensorAnnotation);

	static bool ConvertProto3ToUAsset(FValueInfoProto& OutValueInfoProto, const onnx::ValueInfoProto& InONNXValueInfoProto);

	static bool ConvertProto3ToUAsset(FAttributeProto& OutAttributeProto, const onnx::AttributeProto& InONNXAttributeProto);

	static bool ConvertProto3ToUAsset(FTypeProto& OutTypeProto, const onnx::TypeProto& InONNXTypeProto);

	static bool ConvertProto3ToUAsset(FSparseTensorProto& OutSparseTensorProto, const onnx::SparseTensorProto& InONNXSparseTensorProto);

	static bool ConvertProto3ToUAsset(FTypeProtoTensor& OutTypeProtoTensor, const onnx::TypeProto_Tensor& InONNXTypeProtoTensor);

	static bool ConvertProto3ToUAsset(FTensorProto& OutTensorProto, const onnx::TensorProto& InONNXTensorProto);

	static bool ConvertProto3ToUAsset(FTensorShapeProto& OutTensorShapeProto, const onnx::TensorShapeProto& InONNXTensorShapeProto);

	static bool ConvertProto3ToUAsset(FStringStringEntryProto& OutStringStringEntryProto, const onnx::StringStringEntryProto& FStringStringEntryProto);

	static bool ConvertProto3ToUAsset(FTensorShapeProtoDimension& OutTensorShapeProtoDimension, const onnx::TensorShapeProto_Dimension& InONNXTensorShapeProtoDimension);

	static bool ConvertProto3ToUAsset(FTensorProtoSegment& OutTensorProtoSegment, const onnx::TensorProto_Segment& InONNXTensorProtoSegment);

	static bool ConvertProto3ToUAssetFString(TArray<FString>& OutFStringArray, const google::protobuf::RepeatedPtrField<std::basic_string<char, std::char_traits<char>, std::allocator<char>>>& InONNXFStringArray);

	static bool ConvertProto3ToUAssetUInt8(TArray<uint8>& OutDataArray, const std::string& InRawDataString);

	template <typename T>
	static bool ConvertProto3ToUAssetBasicType(TArray<T>& OutBasicTypeArray, const google::protobuf::RepeatedField<T>& InONNXBasicTypeArray);

	template <typename T, typename S>
	static bool ConvertProto3ToUAssetProtoArrays(TArray<T>& OutProtoArray, const google::protobuf::RepeatedPtrField<S>& InONNXArray);
#endif //WITH_PROTOBUF
};




/* ModelProtoConverter template functions
 *****************************************************************************/

#ifdef WITH_PROTOBUF

template <typename T>
bool FModelProtoConverter::ConvertProto3ToUAssetBasicType(TArray<T>& OutBasicTypeArray, const google::protobuf::RepeatedField<T>& InONNXBasicTypeArray)
{
	const int32 ArraySize = InONNXBasicTypeArray.size();
	OutBasicTypeArray.SetNumUninitialized(ArraySize);
	
	FMemory::Memcpy(OutBasicTypeArray.GetData(), &InONNXBasicTypeArray.Get(0), sizeof(T)*ArraySize);

	// Return
	return true;
}

template <typename T, typename S>
static bool FModelProtoConverter::ConvertProto3ToUAssetProtoArrays(TArray<T>& OutProtoArray, const google::protobuf::RepeatedPtrField<S>& InONNXArray)
{
	const int32 ArraySize = InONNXArray.size();
	OutProtoArray.SetNum(ArraySize);

	for (int32 ArrayIndex = 0; ArrayIndex < ArraySize; ArrayIndex++)
	{
		if (!ConvertProto3ToUAsset(OutProtoArray[ArrayIndex], InONNXArray[ArrayIndex]))
		{
			UE_LOG(LogModelProtoFileReader, Warning, TEXT("FModelProtoConverter::ConvertFromONNXProto3Ifstream(): ConvertProto3toUAssetProtoArrays() failed."));
			return false;
		}
	}
	return true;
}

#endif //WITH_PROTOBUF
