// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundGenerator.h"
#include "MetasoundNodeInterface.h"

namespace Metasound::Test
{
	/** Helper to make testing nodes simpler. */
	class METASOUNDENGINETEST_API FNodeTestGraphBuilder
	{
	public:
		FNodeTestGraphBuilder();

		/** Add a node to the graph */
		Frontend::FNodeHandle AddNode(const FNodeClassName& ClassName, int32 MajorVersion) const;

		/** Add an input node to the graph */
		Frontend::FNodeHandle AddInput(
			const FName& InputName,
			const FName& TypeName,
			EMetasoundFrontendVertexAccessType AccessType = EMetasoundFrontendVertexAccessType::Reference) const;

		/** Add a constructor input to the graph */
		template<typename DataType>
		Frontend::FNodeHandle AddConstructorInput(const FName& InputName, const DataType& Value) const
		{
			check(RootGraph->IsValid());

			FMetasoundFrontendClassInput Input;
			Input.Name = InputName;
			Input.TypeName = GetMetasoundDataTypeName<DataType>();
			Input.VertexID = FGuid::NewGuid();
			Input.AccessType = EMetasoundFrontendVertexAccessType::Value;
			Input.DefaultLiteral.Set(Value);
			return RootGraph->AddInputVertex(Input);
		}

		/** Add an output node to the graph */
		Frontend::FNodeHandle AddOutput(const FName& OutputName, const FName& TypeName);

		TUniquePtr<FMetasoundGenerator> BuildGenerator(FSampleRate SampleRate = 48000, int32 SamplesPerBlock = 256) const;

		/** Helper that will add a single node, wire up the node's inputs and outputs, and hand back the graph's operator */
		static TUniquePtr<FMetasoundGenerator> MakeSingleNodeGraph(
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
