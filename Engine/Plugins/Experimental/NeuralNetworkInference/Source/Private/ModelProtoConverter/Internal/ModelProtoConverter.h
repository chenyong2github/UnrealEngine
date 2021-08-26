// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ModelProto.h"
#include <istream>

class MODELPROTOCONVERTER_API FModelProtoConverter
{
public:
	/**
	 * It creates and fills OutModelProto from the *.onnx file read into InIfstream.
	 * @return False if it could not initialize it, true if successful.
	 */
	static bool ConvertFromONNXProto3Ifstream(FModelProto& OutModelProto, std::istream& InIfstream);
};
