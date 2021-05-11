// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendRegistries.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundFrontendDocumentAccessPtrPrivate
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;

			bool IsMatchingMetasoundClass(const FMetasoundFrontendClassMetadata& InMetadataA, const FMetasoundFrontendClassMetadata& InMetadataB) 
			{
				if (InMetadataA.Type == InMetadataB.Type)
				{
					if (InMetadataA.ClassName == InMetadataB.ClassName)
					{
						return FRegistry::GetRegistryKey(InMetadataA) == FRegistry::GetRegistryKey(InMetadataB);
					}
				}
				return false;
			}

			bool IsMatchingMetasoundClass(const FNodeClassInfo& InNodeClass, const FMetasoundFrontendClassMetadata& InMetadata) 
			{
				if (InNodeClass.NodeType == InMetadata.Type)
				{
					return InNodeClass.LookupKey == FRegistry::GetRegistryKey(InMetadata);
				}
				return false;
			}
		}

		FVertexAccessPtr FNodeAccessPtr::GetInputWithName(const FString& InName)
		{
			auto FindInputWithName = [Name=InName](FMetasoundFrontendNode& InNode) -> FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FVertexAccessPtr>(FindInputWithName);
		}

		FVertexAccessPtr FNodeAccessPtr::GetInputWithVertexID(const FGuid& InID)
		{
			auto FindInputWithID = [ID=InID](FMetasoundFrontendNode& InNode) -> FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FVertexAccessPtr>(FindInputWithID);
		}

		FVertexAccessPtr FNodeAccessPtr::GetOutputWithName(const FString& InName)
		{
			auto FindOutputWithName = [Name=InName](FMetasoundFrontendNode& InNode) -> FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FVertexAccessPtr>(FindOutputWithName);
		}

		FVertexAccessPtr FNodeAccessPtr::GetOutputWithVertexID(const FGuid& InID)
		{
			auto FindOutputWithID = [ID=InID](FMetasoundFrontendNode& InNode) -> FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FVertexAccessPtr>(FindOutputWithID);
		}

		FConstVertexAccessPtr FNodeAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindInputWithName = [Name=InName](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindInputWithName);
		}

		FConstVertexAccessPtr FNodeAccessPtr::GetInputWithVertexID(const FGuid& InID) const
		{
			auto FindInputWithID = [ID=InID](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindInputWithID);
		}

		FConstVertexAccessPtr FNodeAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindOutputWithName = [Name=InName](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindOutputWithName);
		}

		FConstVertexAccessPtr FNodeAccessPtr::GetOutputWithVertexID(const FGuid& InID) const
		{
			auto FindOutputWithID = [ID=InID](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindOutputWithID);
		}

		FConstVertexAccessPtr FConstNodeAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindInputWithName = [Name=InName](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindInputWithName);
		}

		FConstVertexAccessPtr FConstNodeAccessPtr::GetInputWithVertexID(const FGuid& InID) const
		{
			auto FindInputWithID = [ID=InID](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Inputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindInputWithID);
		}

		FConstVertexAccessPtr FConstNodeAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindOutputWithName = [Name=InName](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithName = [&](const FMetasoundFrontendVertex& Vertex) { return Vertex.Name == Name; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithName);
			};

			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindOutputWithName);
		}

		FConstVertexAccessPtr FConstNodeAccessPtr::GetOutputWithVertexID(const FGuid& InID) const
		{
			auto FindOutputWithID = [ID=InID](const FMetasoundFrontendNode& InNode) -> const FMetasoundFrontendVertex*
			{
				auto IsVertexWithVertexID = [=](const FMetasoundFrontendVertex& Vertex) { return Vertex.VertexID == ID; };
				return InNode.Interface.Outputs.FindByPredicate(IsVertexWithVertexID);
			};
			return GetMemberAccessPtr<FConstVertexAccessPtr>(FindOutputWithID);
		}


		// FConstClassAccessPtr
		FClassInputAccessPtr FClassAccessPtr::GetInputWithName(const FString& InName)
		{
			auto FindClassInputWithName = [Name=InName](FMetasoundFrontendClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FClassInputAccessPtr>(FindClassInputWithName);
		}

		FClassOutputAccessPtr FClassAccessPtr::GetOutputWithName(const FString& InName)
		{
			auto FindClassOutputWithName = [Name=InName](FMetasoundFrontendClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FClassOutputAccessPtr>(FindClassOutputWithName);
		}

		FConstClassInputAccessPtr FClassAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindClassInputWithName = [Name=InName](const FMetasoundFrontendClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithName);
		}

		FConstClassOutputAccessPtr FClassAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindClassOutputWithName = [Name=InName](const FMetasoundFrontendClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithName);
		}

		// FConstClassAccessPtr
		FConstClassInputAccessPtr FConstClassAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindClassInputWithName = [Name=InName](const FMetasoundFrontendClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithName);
		}

		FConstClassOutputAccessPtr FConstClassAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindClassOutputWithName = [Name=InName](const FMetasoundFrontendClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithName);
		}

		// FGraphClassAccessPtr
		FClassInputAccessPtr FGraphClassAccessPtr::GetInputWithName(const FString& InName)
		{
			auto FindClassInputWithName = [Name=InName](FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FClassInputAccessPtr>(FindClassInputWithName);
		}

		FClassInputAccessPtr FGraphClassAccessPtr::GetInputWithNodeID(const FGuid& InNodeID)
		{
			auto FindClassInputWithNodeID = [NodeID=InNodeID](FMetasoundFrontendGraphClass& InGraphClass) -> FMetasoundFrontendClassInput*
			{
				return InGraphClass.Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FClassInputAccessPtr>(FindClassInputWithNodeID);
		}

		FClassOutputAccessPtr FGraphClassAccessPtr::GetOutputWithName(const FString& InName)
		{
			auto FindClassOutputWithName = [Name=InName](FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FClassOutputAccessPtr>(FindClassOutputWithName);
		}

		FClassOutputAccessPtr FGraphClassAccessPtr::GetOutputWithNodeID(const FGuid& InNodeID)
		{
			auto FindClassOutputWithNodeID = [NodeID=InNodeID](FMetasoundFrontendGraphClass& InGraphClass) -> FMetasoundFrontendClassOutput*
			{
				return InGraphClass.Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FClassOutputAccessPtr>(FindClassOutputWithNodeID);
		}

		FNodeAccessPtr FGraphClassAccessPtr::GetNodeWithNodeID(const FGuid& InNodeID)
		{
			auto FindNodeWithNodeID = [ID=InNodeID](FMetasoundFrontendGraphClass& InGraphClass) -> FMetasoundFrontendNode*
			{
				auto IsNodeWithID = [&](const FMetasoundFrontendNode& InNode) { return InNode.ID == ID; };
				return InGraphClass.Graph.Nodes.FindByPredicate(IsNodeWithID);
			};

			return GetMemberAccessPtr<FNodeAccessPtr>(FindNodeWithNodeID);
		}

		FGraphAccessPtr FGraphClassAccessPtr::GetGraph()
		{
			auto FindGraph = [](FMetasoundFrontendGraphClass& InGraphClass) -> FMetasoundFrontendGraph* 
			{ 
				return &InGraphClass.Graph; 
			};

			return GetMemberAccessPtr<FGraphAccessPtr>(FindGraph);
		}


		FConstClassInputAccessPtr FGraphClassAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindClassInputWithName = [Name=InName](const FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithName);
		}


		FConstClassInputAccessPtr FGraphClassAccessPtr::GetInputWithNodeID(const FGuid& InNodeID) const
		{
			auto FindClassInputWithNodeID = [NodeID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendClassInput*
			{
				return InGraphClass.Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithNodeID);
		}

		FConstClassOutputAccessPtr FGraphClassAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindClassOutputWithName = [Name=InName](const FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithName);
		}

		FConstClassOutputAccessPtr FGraphClassAccessPtr::GetOutputWithNodeID(const FGuid& InNodeID) const
		{
			auto FindClassOutputWithNodeID = [NodeID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendClassOutput*
			{
				return InGraphClass.Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithNodeID);
		}

		FConstNodeAccessPtr FGraphClassAccessPtr::GetNodeWithNodeID(const FGuid& InNodeID) const
		{
			auto FindNodeWithNodeID = [ID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendNode*
			{
				auto IsNodeWithID = [&](const FMetasoundFrontendNode& InNode) { return InNode.ID == ID; };
				return InGraphClass.Graph.Nodes.FindByPredicate(IsNodeWithID);
			};

			return GetMemberAccessPtr<FConstNodeAccessPtr>(FindNodeWithNodeID);
		}

		FConstGraphAccessPtr FGraphClassAccessPtr::GetGraph() const
		{
			auto FindGraph = [](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendGraph* 
			{ 
				return &InGraphClass.Graph; 
			};

			return GetMemberAccessPtr<FConstGraphAccessPtr>(FindGraph);
		}

		// FConstGraphClassAccessPtr
		FConstClassInputAccessPtr FConstGraphClassAccessPtr::GetInputWithName(const FString& InName) const
		{
			auto FindClassInputWithName = [Name=InName](const FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassInputWithName = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.Name == Name; };
				return InClass.Interface.Inputs.FindByPredicate(IsClassInputWithName);

			};

			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithName);
		}

		FConstClassInputAccessPtr FConstGraphClassAccessPtr::GetInputWithNodeID(const FGuid& InNodeID) const
		{
			auto FindClassInputWithNodeID = [NodeID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendClassInput*
			{
				return InGraphClass.Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FConstClassInputAccessPtr>(FindClassInputWithNodeID);
		}

		FConstClassOutputAccessPtr FConstGraphClassAccessPtr::GetOutputWithName(const FString& InName) const
		{
			auto FindClassOutputWithName = [Name=InName](const FMetasoundFrontendGraphClass& InClass)
			{
				auto IsClassOutputWithName = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.Name == Name; };
				return InClass.Interface.Outputs.FindByPredicate(IsClassOutputWithName);

			};

			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithName);
		}

		FConstClassOutputAccessPtr FConstGraphClassAccessPtr::GetOutputWithNodeID(const FGuid& InNodeID) const
		{
			auto FindClassOutputWithNodeID = [NodeID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendClassOutput*
			{
				return InGraphClass.Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.NodeID == NodeID; });
			};
			return GetMemberAccessPtr<FConstClassOutputAccessPtr>(FindClassOutputWithNodeID);
		}

		FConstNodeAccessPtr FConstGraphClassAccessPtr::GetNodeWithNodeID(const FGuid& InNodeID) const
		{
			auto FindNodeWithNodeID = [ID=InNodeID](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendNode*
			{
				auto IsNodeWithID = [&](const FMetasoundFrontendNode& InNode) { return InNode.ID == ID; };
				return InGraphClass.Graph.Nodes.FindByPredicate(IsNodeWithID);
			};

			return GetMemberAccessPtr<FConstNodeAccessPtr>(FindNodeWithNodeID);
		}

		FConstGraphAccessPtr FConstGraphClassAccessPtr::GetGraph() const
		{
			auto FindGraph = [](const FMetasoundFrontendGraphClass& InGraphClass) -> const FMetasoundFrontendGraph* 
			{ 
				return &InGraphClass.Graph; 
			};

			return GetMemberAccessPtr<FConstGraphAccessPtr>(FindGraph);
		}

		// FDocumentAccessPtr
		
		FGraphClassAccessPtr FDocumentAccessPtr::GetRootGraph()
		{
			return GetMemberAccessPtr<FGraphClassAccessPtr>([](FMetasoundFrontendDocument& InDoc) { return &InDoc.RootGraph; });
		}

		FGraphClassAccessPtr FDocumentAccessPtr::GetSubgraphWithID(const FGuid& InID)
		{
			auto FindSubgraphWithID = [ID=InID](FMetasoundFrontendDocument& InDoc) -> FMetasoundFrontendGraphClass*
			{
				return InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
			};

			return GetMemberAccessPtr<FGraphClassAccessPtr>(FindSubgraphWithID);
		}

		FClassAccessPtr FDocumentAccessPtr::GetDependencyWithID(const FGuid& InID)
		{
			auto FindDependencyWithID = [ID=InID](FMetasoundFrontendDocument& InDoc) -> FMetasoundFrontendClass*
			{
				return InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });
			};
			return GetMemberAccessPtr<FClassAccessPtr>(FindDependencyWithID);
		}


		FClassAccessPtr FDocumentAccessPtr::GetClassWithID(const FGuid& InID) 
		{
			auto FindClassWithID = [ID=InID](FMetasoundFrontendDocument& InDoc) -> FMetasoundFrontendClass*
			{
				FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });

				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
				}

				return MetasoundClass;
			};
			return GetMemberAccessPtr<FClassAccessPtr>(FindClassWithID);
		}

		FClassAccessPtr FDocumentAccessPtr::GetClassWithMetadata(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			auto FindClassWithMetadata = [Metadata=InMetadata](FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithMetadata = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;

					return IsMatchingMetasoundClass(Metadata, InClass.Metadata);
				};

				FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithMetadata);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithMetadata);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FClassAccessPtr>(FindClassWithMetadata);
		}

		FClassAccessPtr FDocumentAccessPtr::GetClassWithInfo(const FNodeClassInfo& InInfo)
		{
			auto FindClassWithInfo = [Info=InInfo](FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithInfo = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;
					return IsMatchingMetasoundClass(Info, InClass.Metadata);
				};

				FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithInfo);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithInfo);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FClassAccessPtr>(FindClassWithInfo);
		}

		FConstGraphClassAccessPtr FDocumentAccessPtr::GetRootGraph() const
		{
			return GetMemberAccessPtr<FConstGraphClassAccessPtr>([](const FMetasoundFrontendDocument& InDoc) { return &InDoc.RootGraph; });
		}

		FConstGraphClassAccessPtr FDocumentAccessPtr::GetSubgraphWithID(const FGuid& InID) const
		{
			auto FindSubgraphWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc)
			{
				return InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
			};
			return GetMemberAccessPtr<FConstGraphClassAccessPtr>(FindSubgraphWithID);
		}

		FConstClassAccessPtr FDocumentAccessPtr::GetDependencyWithID(const FGuid& InID) const
		{
			auto FindDependencyWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc)
			{
				return InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });
			};
			return GetMemberAccessPtr<FConstClassAccessPtr>(FindDependencyWithID);
		}


		FConstClassAccessPtr FDocumentAccessPtr::GetClassWithID(const FGuid& InID) const
		{
			auto FindClassWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc) -> const FMetasoundFrontendClass*
			{
				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });

				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
				}

				return MetasoundClass;
			};
			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithID);
		}

		FConstClassAccessPtr FDocumentAccessPtr::GetClassWithMetadata(const FMetasoundFrontendClassMetadata& InMetadata) const
		{
			auto FindClassWithMetadata = [Metadata=InMetadata](const FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithMetadata = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;
					return IsMatchingMetasoundClass(Metadata, InClass.Metadata);
				};

				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithMetadata);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithMetadata);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithMetadata);
		}

		FConstClassAccessPtr FDocumentAccessPtr::GetClassWithInfo(const FNodeClassInfo& InInfo) const
		{
			auto FindClassWithInfo = [Info=InInfo](const FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithInfo = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;
					return IsMatchingMetasoundClass(Info, InClass.Metadata);
				};

				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithInfo);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithInfo);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithInfo);
		}

		FConstGraphClassAccessPtr FConstDocumentAccessPtr::GetRootGraph() const
		{
			return GetMemberAccessPtr<FConstGraphClassAccessPtr>([](const FMetasoundFrontendDocument& InDoc) { return &InDoc.RootGraph; });
		}

		FConstGraphClassAccessPtr FConstDocumentAccessPtr::GetSubgraphWithID(const FGuid& InID) const
		{
			auto FindSubgraphWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc)
			{
				return InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
			};
			return GetMemberAccessPtr<FConstGraphClassAccessPtr>(FindSubgraphWithID);
		}

		FConstClassAccessPtr FConstDocumentAccessPtr::GetDependencyWithID(const FGuid& InID) const
		{
			auto FindDependencyWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc)
			{
				return InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });
			};
			return GetMemberAccessPtr<FConstClassAccessPtr>(FindDependencyWithID);
		}


		FConstClassAccessPtr FConstDocumentAccessPtr::GetClassWithID(const FGuid& InID) const
		{
			auto FindClassWithID = [ID=InID](const FMetasoundFrontendDocument& InDoc)
			{
				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate([&](const FMetasoundFrontendClass& InDependency) { return InDependency.ID == ID; });

				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate([&](const FMetasoundFrontendGraphClass& InSubgraph) { return InSubgraph.ID == ID; });
				}

				return MetasoundClass;
			};
			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithID);
		}

		FConstClassAccessPtr FConstDocumentAccessPtr::GetClassWithMetadata(const FMetasoundFrontendClassMetadata& InMetadata) const
		{
			auto FindClassWithMetadata = [Metadata=InMetadata](const FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithMetadata = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;
					return IsMatchingMetasoundClass(Metadata, InClass.Metadata);
				};

				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithMetadata);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithMetadata);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithMetadata);
		}

		FConstClassAccessPtr FConstDocumentAccessPtr::GetClassWithInfo(const FNodeClassInfo& InInfo) const
		{
			auto FindClassWithInfo = [Info=InInfo](const FMetasoundFrontendDocument& InDoc)
			{
				auto IsClassWithInfo = [=](const FMetasoundFrontendClass& InClass) 
				{ 
					using namespace MetasoundFrontendDocumentAccessPtrPrivate;
					return IsMatchingMetasoundClass(Info, InClass.Metadata);
				};

				const FMetasoundFrontendClass* MetasoundClass = InDoc.Dependencies.FindByPredicate(IsClassWithInfo);
				if (nullptr == MetasoundClass)
				{
					MetasoundClass = InDoc.Subgraphs.FindByPredicate(IsClassWithInfo);
				}
				return MetasoundClass;
			};

			return GetMemberAccessPtr<FConstClassAccessPtr>(FindClassWithInfo);
		}
	}
}
