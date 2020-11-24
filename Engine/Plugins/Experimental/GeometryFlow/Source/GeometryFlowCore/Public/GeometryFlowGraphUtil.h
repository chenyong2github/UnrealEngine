// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryFlowGraph.h"
#include "GeometryFlowCoreNodes.h"

namespace UE
{
namespace GeometryFlow
{



template<typename SourceNodeType>
void UpdateSourceNodeValue(FGraph& Graph, FGraph::FHandle NodeHandle, const typename SourceNodeType::CppType& NewValue)
{
	Graph.ApplyToNodeOfType<SourceNodeType>(NodeHandle, [&](SourceNodeType& Node)
	{
		Node.UpdateSourceValue(NewValue);
	});
}




template<typename SettingsType>
void UpdateSettingsSourceNodeValue(FGraph& Graph, FGraph::FHandle NodeHandle, const SettingsType& NewSettings)
{
	using SettingsSourceNodeType = TSourceNode<SettingsType, SettingsType::DataTypeIdentifier>;

	Graph.ApplyToNodeOfType<SettingsSourceNodeType>(NodeHandle, [&](SettingsSourceNodeType& Node)
	{
		Node.UpdateSourceValue(NewSettings);
	});
}




}	// end namespace GeometryFlow
}	// end namespace UE