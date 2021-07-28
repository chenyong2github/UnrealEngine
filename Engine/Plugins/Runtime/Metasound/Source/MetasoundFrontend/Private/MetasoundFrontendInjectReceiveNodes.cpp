// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendInjectReceiveNodes.h"

#include "CoreMinimal.h"
#include "MetasoundFacade.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundVertex.h"

namespace Metasound
{
	namespace Frontend 
	{
		namespace MetasoundFrontendInjectReceiveNodesPrivate
		{
			// Metasound Operator which returns the transmission address for the injected receive node.
			class FAddressOperator : public FNoOpOperator
			{
			public:
				static const TCHAR* GetAddressVertexKey();
				static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams&, TArray<TUniquePtr<IOperatorBuildError>>&);
				static const FNodeClassMetadata& GetNodeInfo();

				FAddressOperator(TDataReadReference<FSendAddress> InAddress)
				: Address(InAddress)
				{
				}

				virtual ~FAddressOperator() = default;

				virtual FDataReferenceCollection GetOutputs() const override
				{
					FDataReferenceCollection Outputs;
					Outputs.AddDataReadReference(GetAddressVertexKey(), Address);
					return Outputs;
				}

			private:
				TDataReadReference<FSendAddress> Address;
			};

			// Metasound Node which returns the transmission address for the injected receive node.
			class FAddressNode : public FNodeFacade
			{
			public:
				FAddressNode(const FGuid& InID, const FVertexKey& InVertexKey, const FName& InTypeName, FReceiveNodeAddressFunction InAddressFunction)
				: FNodeFacade(FString::Format(TEXT("ReceiveAddressInject_{0}"), { InVertexKey }), InID, TFacadeOperatorClass<FAddressOperator>())
				, VertexKey(InVertexKey)
				, TypeName(InTypeName)
				, AddressFunction(InAddressFunction)
				{
				}

				FSendAddress GetAddress(const FMetasoundEnvironment& InEnvironment) const
				{
					return AddressFunction(InEnvironment, VertexKey, TypeName);
				}

			private:
				FVertexKey VertexKey;
				FName TypeName;
				FReceiveNodeAddressFunction AddressFunction;
			};

			const TCHAR* FAddressOperator::GetAddressVertexKey() { return TEXT("Address"); }

			TUniquePtr<IOperator> FAddressOperator::CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutBuildErrors)
			{
				const FAddressNode& AddressNode = static_cast<const FAddressNode&>(InParams.Node);
				FSendAddress Address = AddressNode.GetAddress(InParams.Environment);
				return MakeUnique<FAddressOperator>(TDataReadReference<FSendAddress>::CreateNew(Address));
			}

			const FNodeClassMetadata& FAddressOperator::GetNodeInfo()
			{
				auto CreateVertexInterface = []() -> FVertexInterface
				{
					FVertexInterface Interface
					{
						FInputVertexInterface{},
						FOutputVertexInterface
						{
							TOutputDataVertexModel<FSendAddress>(FAddressOperator::GetAddressVertexKey(), FText::GetEmpty())
						}
					};

					return Interface;
				};

				auto CreateNodeClassMetadata = [&CreateVertexInterface]() -> FNodeClassMetadata
				{
					FNodeClassMetadata Metadata
					{
						FNodeClassName{"MetasoundFrontendInjectReceiveNodes", "ReceiveNodeAddress", ""},
						1, // MajorVersion
						0, // MinorVersion
						FText::GetEmpty(), // DisplayName
						FText::GetEmpty(), // Description
						FText::GetEmpty(), // Author
						FText::GetEmpty(), // Prompt If Missing
						CreateVertexInterface(), // DefaultInterface
						TArray<FText>(), // CategoryHierachy
						TArray<FName>(), // Keywoards
						FNodeDisplayStyle {} // NodeDisplayStyle
					}; 

					return Metadata;
				};

				static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
				return Metadata;
			}

			TUniquePtr<INode> CreateReceiveNodeForDataType(const FGuid& InID, const FVertexKey& InVertexKey, const FName& InDataType)
			{
				const EMetasoundFrontendClassType Type = EMetasoundFrontendClassType::External;
				const FNodeClassName& ClassName = ReceiveNodeInfo::GetClassNameForDataType(InDataType);
				const int32 MajorVersion = ReceiveNodeInfo::GetCurrentMajorVersion();
				const int32 MinorVersion = ReceiveNodeInfo::GetCurrentMinorVersion();

				FNodeRegistryKey ReceiveNodeRegistryKey = NodeRegistryKey::CreateKey(Type, ClassName.GetFullName().ToString(), MajorVersion, MinorVersion);
				FNodeInitData ReceiveNodeInitData 
				{ 
					FString::Format(TEXT("ReceiveInject_{0}"), { InVertexKey }),
					InID
				};
				return FMetasoundFrontendRegistryContainer::Get()->CreateNode(ReceiveNodeRegistryKey, MoveTemp(ReceiveNodeInitData));
			}
		}

		bool InjectReceiveNode(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressFunction, const FInputDataDestination& InputDestination)
		{
			using namespace MetasoundFrontendInjectReceiveNodesPrivate;
			// should never contain null nodes for input destination.
			check(InputDestination.Node != nullptr);

			const FVertexKey& VertexKey = InputDestination.Vertex.GetVertexName();
			const FName& DataType = InputDestination.Vertex.GetDataTypeName();

			// Create a receive node.
			const FGuid ReceiveNodeID = FGuid::NewGuid();
			TSharedPtr<INode> ReceiveNode(CreateReceiveNodeForDataType(ReceiveNodeID, VertexKey, DataType).Release());

			if (!ReceiveNode.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to create receive node while injecting receive node for graph input [VertexKey:%s, VertexDescription:%s, DataTypeName:%s]"), *VertexKey, *InputDestination.Vertex.GetMetadata().Description.ToString(), *DataType.ToString());
				return false;
			}

			// Create a node for address of receive node.
			const FGuid AddressNodeID = FGuid::NewGuid();
			TSharedPtr<INode> AddressNode = MakeShared<FAddressNode>(AddressNodeID, VertexKey, DataType, InAddressFunction);

			if (!AddressNode.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to create address node while injecting receive node for graph input [VertexKey:%s, VertexDescription:%s, DataTypeName:%s]"), *VertexKey, *InputDestination.Vertex.GetMetadata().Description.ToString(), *DataType.ToString());
				return false;
			}

			// Add receive node and value node to graph.
			InGraph.AddNode(ReceiveNodeID, ReceiveNode);
			InGraph.AddNode(AddressNodeID, AddressNode);
			check(InGraph.AddDataEdge(*AddressNode, FAddressOperator::GetAddressVertexKey(), *ReceiveNode, ReceiveNodeInfo::GetAddressInputName()));
			
			auto IsEdgeConnectedToCurrentInput = [&InputDestination](const FDataEdge& InEdge)
			{
				return (InEdge.From.Node == InputDestination.Node) && (InEdge.From.Vertex.GetVertexName() == InputDestination.Vertex.GetVertexName());
			};

			// Cache previous connections from input node.
			TArray<FDataEdge> EdgesFromInput = InGraph.GetDataEdges().FilterByPredicate(IsEdgeConnectedToCurrentInput);
			// Remove existing connections from input node.
			InGraph.RemoveDataEdgeByPredicate(IsEdgeConnectedToCurrentInput);

			// Connect previous connections to receive node output.
			FOutputDataSource ReceiveOutputSource { *ReceiveNode, ReceiveNode->GetVertexInterface().GetOutputVertex(ReceiveNodeInfo::GetOutputName()) };
			for (const FDataEdge& Edge : EdgesFromInput)
			{
				FDataEdge NewEdge { ReceiveOutputSource, Edge.To };
				InGraph.AddDataEdge(FDataEdge{ ReceiveOutputSource, Edge.To });
			}

			// Connect input node to receive node.
			check(InGraph.AddDataEdge(*InputDestination.Node, VertexKey, *ReceiveNode, ReceiveNodeInfo::GetDefaultDataInputName()));

			return true;
		}

		bool InjectReceiveNodes(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressFunction, const TSet<FVertexKey>& InInputVerticesToSkip)
		{
			using namespace Metasound::Frontend;
			bool bSuccess = true;

			const FInputDataDestinationCollection& InputDestinations = InGraph.GetInputDataDestinations();

			for (const FInputDataDestinationCollection::ElementType& Pair : InputDestinations)
			{
				const FInputDataDestination& InputDestination = Pair.Value;
				if (!ensure(InputDestination.Node != nullptr))
				{
					// should never contain null nodes for input destination.
					bSuccess = false;
					continue;
				}

				const FVertexKey& VertexKey = InputDestination.Vertex.GetVertexName();
				if (InInputVerticesToSkip.Contains(VertexKey))
				{
					// Skip this key as requested by caller.
					continue;
				}

				const bool bInjectionSuccess = InjectReceiveNode(InGraph, InAddressFunction, InputDestination);
				bSuccess = bSuccess || bInjectionSuccess;
			}

			return bSuccess;
		}
	}
}

