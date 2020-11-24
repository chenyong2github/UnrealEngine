// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowCoreNodes.h"
#include "GeometryFlowMovableData.h"

namespace UE
{
namespace GeometryFlow
{



template<typename DataType>
TSafeSharedPtr<TMovableData<DataType, DataType::DataTypeIdentifier>> MakeMovableData(DataType&& Data)
{
	return MakeShared<TMovableData<DataType, DataType::DataTypeIdentifier>, ESPMode::ThreadSafe>(MoveTemp(Data));
}


template<typename DataType>
TUniquePtr<TBasicNodeInput<DataType, DataType::DataTypeIdentifier>> MakeBasicInput()
{
	return MakeUnique<TBasicNodeInput<DataType, DataType::DataTypeIdentifier>>();
}

template<typename DataType>
TUniquePtr<TBasicNodeOutput<DataType, DataType::DataTypeIdentifier>> MakeBasicOutput()
{
	return MakeUnique<TBasicNodeOutput<DataType, DataType::DataTypeIdentifier>>();
}






}	// end namespace GeometryFlow
}	// end n