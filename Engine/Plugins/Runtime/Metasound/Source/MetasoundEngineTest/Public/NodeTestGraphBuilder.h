// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundGenerator.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"

namespace Metasound::Test
{
	/** Helper to make testing nodes simpler. */
	class METASOUNDENGINETEST_API FNodeTestGraphBuilder
	{
	public:
		FNodeTestGraphBuilder();

		/** Add a node to the graph */
		Frontend::FNodeHandle AddNode(const FNodeClassName& ClassName, int32 MajorVersion);

		/** Add an input node to the graph */
		Frontend::FNodeHandle AddInput(const FName& InputName, const FName& TypeName);

		/** Add an output node to the graph */
		Frontend::FNodeHandle AddOutput(const FName& OutputName, const FName& TypeName);

		/** Build the graph and get the graph's operator to work with */
		TUniquePtr<IOperator> BuildGraph(FSampleRate SampleRate = 48000, int32 SamplesPerBlock = 256);

		TUniquePtr<FMetasoundGenerator> BuildGenerator(FSampleRate SampleRate = 48000, int32 SamplesPerBlock = 256);

		/** Helper that will add a single node, wire up the node's inputs and outputs, and hand back the graph's operator */
		static TUniquePtr<IOperator> MakeSingleNodeGraph(
			const FNodeClassName& ClassName,
			int32 MajorVersion,
			FSampleRate SampleRate = 48000,
			int32 SamplesPerBlock = 256);

	private:
		FMetasoundFrontendDocument Document;
		Frontend::FDocumentHandle DocumentHandle = Frontend::IDocumentController::GetInvalidHandle();
		Frontend::FGraphHandle RootGraph = Frontend::IGraphController::GetInvalidHandle();
		TArray<FVertexName> AudioOutputNames;
	};
}
