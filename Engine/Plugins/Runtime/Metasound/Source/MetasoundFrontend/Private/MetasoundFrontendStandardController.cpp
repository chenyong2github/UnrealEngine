// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendStandardController.h"

#include "Algo/ForEach.h"
#include "Algo/NoneOf.h"
#include "Algo/Transform.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundFrontendSubgraphNodeController.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundTrace.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "Serialization/MemoryReader.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontendStandardController"

static int32 MetaSoundAutoUpdateNativeClassCVar = 1;
FAutoConsoleVariableRef CVarMetaSoundAutoUpdateNativeClass(
	TEXT("au.MetaSounds.AutoUpdate.NativeClasses"),
	MetaSoundAutoUpdateNativeClassCVar,
	TEXT("If true, node references to native class that share a version number will attempt to auto-update if the interface is different, which results in slower graph load times.\n")
	TEXT("0: Don't auto-update native classes, !0: Auto-update native classes (default)"),
	ECVF_Default);

namespace Metasound
{
	namespace Frontend
	{
		namespace FrontendControllerIntrinsics
		{
			namespace NodeLayout
			{
				static const FVector2D BufferX{ 250.0f, 0.0f };
				static const FVector2D BufferY{ 0.0f, 100.0f };
			}

			// utility function for returning invalid values. If an invalid value type
			// needs special construction, this template can be specialized. 
			template<typename ValueType>
			ValueType GetInvalidValue()
			{
				ValueType InvalidValue;
				return InvalidValue;
			}

			// Invalid value specialization for int32
			template<>
			int32 GetInvalidValue<int32>() { return INDEX_NONE; }

			// Invalid value specialization for EMetasoundFrontendClassType
			template<>
			EMetasoundFrontendClassType GetInvalidValue<EMetasoundFrontendClassType>() { return EMetasoundFrontendClassType::Invalid; }

			// Invalid value specialization for FText
			template<>
			FText GetInvalidValue<FText>() { return FText::GetEmpty(); }

			template<typename ValueType>
			const ValueType& GetInvalidValueConstRef()
			{
				static const ValueType Value = GetInvalidValue<ValueType>();
				return Value;
			}
		} // namespace FrontendControllerIntrinsics


		FDocumentAccess IDocumentAccessor::GetSharedAccess(IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}

		FConstDocumentAccess IDocumentAccessor::GetSharedAccess(const IDocumentAccessor& InDocumentAccessor)
		{
			return InDocumentAccessor.ShareAccess();
		}


		//
		// FBaseOutputController
		//
		FBaseOutputController::FBaseOutputController(const FBaseOutputController::FInitParams& InParams)
		: ID(InParams.ID)
		, NodeVertexPtr(InParams.NodeVertexPtr)
		, ClassOutputPtr(InParams.ClassOutputPtr)
		, GraphPtr(InParams.GraphPtr)
		, OwningNode(InParams.OwningNode)
		{
		}

		bool FBaseOutputController::IsValid() const
		{
			return OwningNode->IsValid() && (nullptr != NodeVertexPtr.Get()) && (nullptr != GraphPtr.Get());
		}

		FGuid FBaseOutputController::GetID() const
		{
			return ID;
		}

		const FName& FBaseOutputController::GetDataType() const
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				return Vertex->TypeName;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FName>();
		}

		const FString& FBaseOutputController::GetName() const
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				return Vertex->Name;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		FGuid FBaseOutputController::GetOwningNodeID() const
		{
			return OwningNode->GetID();
		}

		FNodeHandle FBaseOutputController::GetOwningNode()
		{
			return OwningNode;
		}

		FConstNodeHandle FBaseOutputController::GetOwningNode() const
		{
			return OwningNode;
		}

		bool FBaseOutputController::IsConnected() const 
		{
			return (FindEdges().Num() > 0);
		}

		TArray<FInputHandle> FBaseOutputController::GetConnectedInputs() 
		{
			TArray<FInputHandle> Inputs;

			// Create output handle from output node.
			FGraphHandle Graph = OwningNode->GetOwningGraph();

			for (const FMetasoundFrontendEdge& Edge : FindEdges())
			{
				FNodeHandle InputNode = Graph->GetNodeWithID(Edge.ToNodeID);

				FInputHandle Input = InputNode->GetInputWithID(Edge.ToVertexID);
				if (Input->IsValid())
				{
					Inputs.Add(Input);
				}
			}

			return Inputs;
		}

		TArray<FConstInputHandle> FBaseOutputController::GetConstConnectedInputs() const 
		{
			TArray<FConstInputHandle> Inputs;

			// Create output handle from output node.
			FConstGraphHandle Graph = OwningNode->GetOwningGraph();

			for (const FMetasoundFrontendEdge& Edge : FindEdges())
			{
				FConstNodeHandle InputNode = Graph->GetNodeWithID(Edge.ToNodeID);

				FConstInputHandle Input = InputNode->GetInputWithID(Edge.ToVertexID);
				if (Input->IsValid())
				{
					Inputs.Add(Input);
				}
			}

			return Inputs;
		}

		bool FBaseOutputController::Disconnect() 
		{
			bool bSuccess = true;
			for (FInputHandle Input : GetConnectedInputs())
			{
				if (Input->IsValid())
				{
					bSuccess &= Disconnect(*Input);
				}
			}
			return bSuccess;
		}

		FConnectability FBaseOutputController::CanConnectTo(const IInputController& InController) const
		{
			return InController.CanConnectTo(*this);
		}

		bool FBaseOutputController::Connect(IInputController& InController)
		{
			return InController.Connect(*this);
		}

		bool FBaseOutputController::ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return InController.ConnectWithConverterNode(*this, InNodeClassName);
		}

		bool FBaseOutputController::Disconnect(IInputController& InController)
		{
			return InController.Disconnect(*this);
		}

		TArray<FMetasoundFrontendEdge> FBaseOutputController::FindEdges() const
		{
			if (const FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingSource = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.FromNodeID == NodeID) && (Edge.FromVertexID == VertexID);
				};

				return Graph->Edges.FilterByPredicate(EdgeHasMatchingSource);
			}

			return TArray<FMetasoundFrontendEdge>();
		}

		const FText& FBaseOutputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* Output = ClassOutputPtr.Get())
			{
				return Output->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FBaseOutputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassOutput* Output = ClassOutputPtr.Get())
			{
				return Output->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FBaseOutputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassOutput* Output = ClassOutputPtr.Get())
			{
				return Output->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FBaseOutputController::ShareAccess()
		{
			FDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassOutput = ClassOutputPtr;
			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FBaseOutputController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassOutput = ClassOutputPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		//
		// FInputNodeOutputController
		// 
		FInputNodeOutputController::FInputNodeOutputController(const FInputNodeOutputController::FInitParams& InParams)
		: FBaseOutputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassOutputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		{
		}

		bool FInputNodeOutputController::IsValid() const
		{
			return FBaseOutputController::IsValid() && (nullptr != OwningGraphClassInputPtr.Get());
		}

		const FText& FInputNodeOutputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				if (const FMetasoundFrontendClassOutput* ClassOutput = ClassOutputPtr.Get())
				{
					// If there is a valid ClassOutput, combine the names.
					CachedDisplayName = FText::Format(LOCTEXT("InputNodeOutputControllerFormat", "{1} {0}"), OwningInput->Metadata.DisplayName, ClassOutput->Metadata.DisplayName);
				}
				else
				{
					// If there is no valid ClassOutput, use the owning value display name.
					CachedDisplayName = OwningInput->Metadata.DisplayName;
				}
			}

			return CachedDisplayName;
		}

		const FText& FInputNodeOutputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassInput* Input = OwningGraphClassInputPtr.Get())
			{
				return Input->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FInputNodeOutputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassInput* Input = OwningGraphClassInputPtr.Get())
			{
				return Input->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FInputNodeOutputController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseOutputController::ShareAccess();

			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		FConstDocumentAccess FInputNodeOutputController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseOutputController::ShareAccess();

			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		//
		// FOutputNodeOutputController
		//
		FOutputNodeOutputController::FOutputNodeOutputController(const FOutputNodeOutputController::FInitParams& InParams)
		: FBaseOutputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassOutputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		bool FOutputNodeOutputController::IsValid() const
		{
			return FBaseOutputController::IsValid() && (nullptr != OwningGraphClassOutputPtr.Get());
		}

		const FText& FOutputNodeOutputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FOutputNodeOutputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FOutputNodeOutputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FConnectability FOutputNodeOutputController::CanConnectTo(const IInputController& InController) const 
		{
			// Cannot connect to a graph's output.
			static const FConnectability Connectability = {FConnectability::EConnectable::No};

			return Connectability;
		}

		bool FOutputNodeOutputController::Connect(IInputController& InController) 
		{
			return false;
		}

		bool FOutputNodeOutputController::ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return false;
		}

		//
		// FBaseInputController
		// 
		FBaseInputController::FBaseInputController(const FBaseInputController::FInitParams& InParams)
		: ID(InParams.ID)
		, NodeVertexPtr(InParams.NodeVertexPtr)
		, ClassInputPtr(InParams.ClassInputPtr)
		, GraphPtr(InParams.GraphPtr)
		, OwningNode(InParams.OwningNode)
		{
		}

		bool FBaseInputController::IsValid() const 
		{
			return OwningNode->IsValid() && (nullptr != NodeVertexPtr.Get()) &&  (nullptr != GraphPtr.Get());
		}

		FGuid FBaseInputController::GetID() const
		{
			return ID;
		}

		const FName& FBaseInputController::GetDataType() const
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				return Vertex->TypeName;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FName>();
		}

		const FString& FBaseInputController::GetName() const
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				return Vertex->Name;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		const FText& FBaseInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata.DisplayName;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendLiteral* FBaseInputController::GetLiteral() const
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				return OwningNode->GetInputLiteral(Vertex->VertexID);
			}

			return nullptr;
		}

		void FBaseInputController::SetLiteral(const FMetasoundFrontendLiteral& InLiteral)
		{
			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				if (const FMetasoundFrontendLiteral* ClassLiteral = GetClassDefaultLiteral())
				{
					// Clear if equivalent to class default as fallback is the class default
					if (ClassLiteral->IsEquivalent(InLiteral))
					{
						OwningNode->ClearInputLiteral(Vertex->VertexID);
						return;
					}
				}

				OwningNode->SetInputLiteral(FMetasoundFrontendVertexLiteral{ Vertex->VertexID, InLiteral });
			}
		}

		const FMetasoundFrontendLiteral* FBaseInputController::GetClassDefaultLiteral() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return &(ClassInput->DefaultLiteral);
			}
			return nullptr;
		}

		const FText& FBaseInputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FBaseInputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
			{
				return ClassInput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		bool FBaseInputController::IsConnected() const 
		{
			return (nullptr != FindEdge());
		}

		FGuid FBaseInputController::GetOwningNodeID() const
		{
			return OwningNode->GetID();
		}

		FNodeHandle FBaseInputController::GetOwningNode()
		{
			return OwningNode;
		}

		FConstNodeHandle FBaseInputController::GetOwningNode() const
		{
			return OwningNode;
		}

		FOutputHandle FBaseInputController::GetConnectedOutput()
		{
			if (const FMetasoundFrontendEdge* Edge = FindEdge())
			{
				// Create output handle from output node.
				FGraphHandle Graph = OwningNode->GetOwningGraph();
				FNodeHandle OutputNode = Graph->GetNodeWithID(Edge->FromNodeID);
				return OutputNode->GetOutputWithID(Edge->FromVertexID);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConstOutputHandle FBaseInputController::GetConnectedOutput() const
		{
			if (const FMetasoundFrontendEdge* Edge = FindEdge())
			{
				// Create output handle from output node.
				FConstGraphHandle Graph = OwningNode->GetOwningGraph();
				FConstNodeHandle OutputNode = Graph->GetNodeWithID(Edge->FromNodeID);
				return OutputNode->GetOutputWithID(Edge->FromVertexID);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConnectability FBaseInputController::CanConnectTo(const IOutputController& InController) const
		{
			FConnectability OutConnectability;
			OutConnectability.Connectable = FConnectability::EConnectable::No;

			const FName& DataType = GetDataType();
			const FName& OtherDataType = InController.GetDataType();

			if (!DataType.IsValid())
			{
				return OutConnectability;
			}

			if (OtherDataType == DataType)
			{
				// If data types are equal, connection can happen.
				OutConnectability.Connectable = FConnectability::EConnectable::Yes;
				return OutConnectability;
			}

			// If data types are not equal, check for converter nodes which could
			// convert data type.
			OutConnectability.PossibleConverterNodeClasses = FRegistry::Get()->GetPossibleConverterNodes(OtherDataType, DataType);

			if (OutConnectability.PossibleConverterNodeClasses.Num() > 0)
			{
				OutConnectability.Connectable = FConnectability::EConnectable::YesWithConverterNode;
				return OutConnectability;
			}

			return OutConnectability;
		}

		bool FBaseInputController::Connect(IOutputController& InController)
		{
			const FName& DataType = GetDataType();
			const FName& OtherDataType = InController.GetDataType();

			if (!DataType.IsValid())
			{
				return false;
			}

			if (FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				if (ensureAlwaysMsgf(OtherDataType == DataType, TEXT("Cannot connect incompatible types.")))
				{
					// Overwrite an existing connection if it exists.
					FMetasoundFrontendEdge* Edge = FindEdge();

					if (!Edge)
					{
						Edge = &Graph->Edges.AddDefaulted_GetRef();
						Edge->ToNodeID = GetOwningNodeID();
						Edge->ToVertexID = GetID();
					}

					Edge->FromNodeID = InController.GetOwningNodeID();
					Edge->FromVertexID = InController.GetID();

					return true;
				}
			}

			return false;
		}

		bool FBaseInputController::ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InConverterInfo)
		{
			FGraphHandle OwningGraph = OwningNode->GetOwningGraph();

			// Generate the converter node.
			FNodeHandle ConverterNode = OwningGraph->AddNode(InConverterInfo.NodeKey);

			TArray<FInputHandle> ConverterInputs = ConverterNode->GetInputsWithVertexName(InConverterInfo.PreferredConverterInputPin);
			TArray<FOutputHandle> ConverterOutputs = ConverterNode->GetOutputsWithVertexName(InConverterInfo.PreferredConverterOutputPin);

			if (ConverterInputs.Num() < 1)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Converter node [Name: %s] does not support preferred input vertex [Vertex: %s]"), *ConverterNode->GetNodeName(), *InConverterInfo.PreferredConverterInputPin);
				return false;
			}

			if (ConverterOutputs.Num() < 1)
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Converter node [Name: %s] does not support preferred output vertex [Vertex: %s]"), *ConverterNode->GetNodeName(), *InConverterInfo.PreferredConverterOutputPin);
				return false;
			}

			FInputHandle ConverterInput = ConverterInputs[0];
			FOutputHandle ConverterOutput = ConverterOutputs[0];

			// Connect the output InController to the converter, than connect the converter to this input.
			if (ConverterInput->Connect(InController) && Connect(*ConverterOutput))
			{
				return true;
			}

			return false;
		}

		bool FBaseInputController::Disconnect(IOutputController& InController) 
		{
			if (FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				FGuid FromNodeID = InController.GetOwningNodeID();
				FGuid FromVertexID = InController.GetID();
				FGuid ToNodeID = GetOwningNodeID();
				FGuid ToVertexID = GetID();

				auto IsMatchingEdge = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.FromNodeID == FromNodeID) && (Edge.FromVertexID == FromVertexID) && (Edge.ToNodeID == ToNodeID) && (Edge.ToVertexID == ToVertexID);
				};

				int32 NumRemoved = Graph->Edges.RemoveAllSwap(IsMatchingEdge);
				return NumRemoved > 0;
			}

			return false;
		}

		bool FBaseInputController::Disconnect()
		{
			if (FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				int32 NumRemoved = Graph->Edges.RemoveAllSwap(EdgeHasMatchingDestination);
				return NumRemoved > 0;
			}

			return false;
		}

		const FMetasoundFrontendEdge* FBaseInputController::FindEdge() const
		{
			if (const FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				return Graph->Edges.FindByPredicate(EdgeHasMatchingDestination);
			}

			return nullptr;
		}

		FMetasoundFrontendEdge* FBaseInputController::FindEdge()
		{
			if (FMetasoundFrontendGraph* Graph = GraphPtr.Get())
			{
				const FGuid NodeID = GetOwningNodeID();
				FGuid VertexID = GetID();

				auto EdgeHasMatchingDestination = [&](const FMetasoundFrontendEdge& Edge)
				{
					return (Edge.ToNodeID == NodeID) && (Edge.ToVertexID == VertexID);
				};

				return Graph->Edges.FindByPredicate(EdgeHasMatchingDestination);
			}

			return nullptr;
		}

		FDocumentAccess FBaseInputController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassInput = ClassInputPtr;
			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FBaseInputController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstVertex = NodeVertexPtr;
			Access.ConstClassInput = ClassInputPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}


		//
		// FOutputNodeInputController
		//
		FOutputNodeInputController::FOutputNodeInputController(const FOutputNodeInputController::FInitParams& InParams)
		: FBaseInputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassInputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		bool FOutputNodeInputController::IsValid() const
		{
			return FBaseInputController::IsValid() && (nullptr != OwningGraphClassOutputPtr.Get());
		}

		const FText& FOutputNodeInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
				{
					// If there the ClassInput exists, combine the variable name and class input name.
					// of the variable should be added to the names of the vertices.
					CachedDisplayName = FText::Format(LOCTEXT("OutputNodeInputControllerFormat", "{1} {0}"), OwningOutput->Metadata.DisplayName, ClassInput->Metadata.DisplayName);
				}
				else
				{
					// If there is not ClassInput, then use the variable name.
					CachedDisplayName = OwningOutput->Metadata.DisplayName;
				}
			}

			return CachedDisplayName;
		}

		const FText& FOutputNodeInputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FOutputNodeInputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FDocumentAccess FOutputNodeInputController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseInputController::ShareAccess();

			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}

		FConstDocumentAccess FOutputNodeInputController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseInputController::ShareAccess();

			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}


		//
		// FInputNodeInputController
		//
		FInputNodeInputController::FInputNodeInputController(const FInputNodeInputController::FInitParams& InParams)
		: FBaseInputController({InParams.ID, InParams.NodeVertexPtr, InParams.ClassInputPtr, InParams.GraphPtr, InParams.OwningNode})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		{
		}

		bool FInputNodeInputController::IsValid() const
		{
			return FBaseInputController::IsValid() && (nullptr != OwningGraphClassInputPtr.Get());
		}

		const FText& FInputNodeInputController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeInputController::GetTooltip() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.Description;
			}
			
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FMetasoundFrontendVertexMetadata& FInputNodeInputController::GetMetadata() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendVertexMetadata>();
		}

		FConnectability FInputNodeInputController::CanConnectTo(const IOutputController& InController) const 
		{
			static const FConnectability Connectability = {FConnectability::EConnectable::No};
			return Connectability;
		}

		bool FInputNodeInputController::Connect(IOutputController& InController) 
		{
			return false;
		}

		bool FInputNodeInputController::ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName)
		{
			return false;
		}

		//
		// FBaseNodeController
		//
		FBaseNodeController::FBaseNodeController(const FBaseNodeController::FInitParams& InParams)
		: NodePtr(InParams.NodePtr)
		, ClassPtr(InParams.ClassPtr)
		, OwningGraph(InParams.OwningGraph)
		{
			if (FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
				{
					if (Node->ClassID != Class->ID)
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Changing node's class id from [ClassID:%s] to [ClassID:%s]"), *Node->ClassID.ToString(), *Class->ID.ToString());
						Node->ClassID = Class->ID;
					}
				}
			}
		}

		bool FBaseNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && (nullptr != NodePtr.Get()) && (nullptr != ClassPtr.Get());
		}

		FGuid FBaseNodeController::GetOwningGraphClassID() const
		{
			return OwningGraph->GetClassID();
		}

		FGraphHandle FBaseNodeController::GetOwningGraph()
		{
			return OwningGraph;
		}

		FConstGraphHandle FBaseNodeController::GetOwningGraph() const
		{
			return OwningGraph;
		}

		FGuid FBaseNodeController::GetID() const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				return Node->ID;
			}
			return Metasound::FrontendInvalidID;
		}

		FGuid FBaseNodeController::GetClassID() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->ID;
			}
			return Metasound::FrontendInvalidID;
		}

		const FMetasoundFrontendLiteral* FBaseNodeController::GetInputLiteral(const FGuid& InVertexID) const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				for (const FMetasoundFrontendVertexLiteral& VertexLiteral : Node->InputLiterals)
				{
					if (VertexLiteral.VertexID == InVertexID)
					{
						return &VertexLiteral.Value;
					}
				}
			}

			return nullptr;
		}

		void FBaseNodeController::SetInputLiteral(const FMetasoundFrontendVertexLiteral& InVertexLiteral)
		{
			if (FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				auto IsInputVertex = [InVertexLiteral] (const FMetasoundFrontendVertex& Vertex)
				{
					return InVertexLiteral.VertexID == Vertex.VertexID;
				};

				FMetasoundFrontendNodeInterface& NodeInterface = Node->Interface;
				if (!ensure(NodeInterface.Inputs.ContainsByPredicate(IsInputVertex)))
				{
					return;
				}

				for (FMetasoundFrontendVertexLiteral& VertexLiteral : Node->InputLiterals)
				{
					if (VertexLiteral.VertexID == InVertexLiteral.VertexID)
					{
						if (ensure(VertexLiteral.Value.GetType() == InVertexLiteral.Value.GetType()))
						{
							VertexLiteral = InVertexLiteral;
						}
						return;
					}
				}

				Node->InputLiterals.Add(InVertexLiteral);
			}
		}

		bool FBaseNodeController::ClearInputLiteral(FGuid InVertexID)
		{
			if (FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				auto IsInputVertex = [InVertexID](const FMetasoundFrontendVertexLiteral& VertexLiteral)
				{
					return InVertexID == VertexLiteral.VertexID;
				};

				return Node->InputLiterals.RemoveAllSwap(IsInputVertex, false) > 0;
			}

			return false;
		}

		const FMetasoundFrontendClassInterface& FBaseNodeController::GetClassInterface() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Interface;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassInterface>();
		}

		const FMetasoundFrontendClassMetadata& FBaseNodeController::GetClassMetadata() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Metadata;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassMetadata>();
		}

		const FMetasoundFrontendInterfaceStyle& FBaseNodeController::GetInputStyle() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Interface.GetInputStyle();
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendInterfaceStyle>();
		}

		const FMetasoundFrontendInterfaceStyle& FBaseNodeController::GetOutputStyle() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Interface.GetOutputStyle();
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendInterfaceStyle>();
		}

		const FMetasoundFrontendClassStyle& FBaseNodeController::GetClassStyle() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Style;
			}

			static const FMetasoundFrontendClassStyle Invalid;
			return Invalid;
		}

		const FMetasoundFrontendNodeStyle& FBaseNodeController::GetNodeStyle() const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				return Node->Style;
			}

			static const FMetasoundFrontendNodeStyle Invalid;
			return Invalid;
		}

		void FBaseNodeController::SetNodeStyle(const FMetasoundFrontendNodeStyle& InStyle)
		{
			if (FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				Node->Style = InStyle;
			}
		}

		const FText& FBaseNodeController::GetDescription() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Metadata.GetDescription();
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FString& FBaseNodeController::GetNodeName() const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				return Node->Name;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FString>();
		}

		bool FBaseNodeController::CanAddInput(const FString& InVertexName) const
		{
			// TODO: not yet supported
			return false;
		}

		FInputHandle FBaseNodeController::AddInput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault)
		{
			checkNoEntry();
			// TODO: not yet supported
			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::RemoveInput(FGuid InVertexID)
		{
			checkNoEntry();
			// TODO: not yet supported
			return false;
		}

		bool FBaseNodeController::CanAddOutput(const FString& InVertexName) const
		{
			// TODO: not yet supported
			return false;
		}

		FInputHandle FBaseNodeController::AddOutput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault)
		{
			checkNoEntry();
			// TODO: not yet supported
			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::RemoveOutput(FGuid InVertexID)
		{
			checkNoEntry();
			// TODO: not yet supported
			return false;
		}

		TArray<FInputHandle> FBaseNodeController::GetInputs()
		{
			TArray<FInputHandle> Inputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		int32 FBaseNodeController::GetNumInputs() const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				return Node->Interface.Inputs.Num();
			}

			return 0;
		}

		void FBaseNodeController::IterateInputs(TUniqueFunction<void(FInputHandle)> InFunction)
		{
			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, AsShared());
				if (InputHandle->IsValid())
				{
					InFunction(InputHandle);
				}
			}
		}

		TArray<FOutputHandle> FBaseNodeController::GetOutputs()
		{
			TArray<FOutputHandle> Outputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		int32 FBaseNodeController::GetNumOutputs() const
		{
			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				return Node->Interface.Outputs.Num();
			}

			return 0;
		}

		TArray<FConstInputHandle> FBaseNodeController::GetConstInputs() const
		{
			TArray<FConstInputHandle> Inputs;

			// If I had a nickle for every time C++ backed me into a corner, I would be sitting
			// on a tropical beach next to my mansion sipping strawberry daiquiris instead of 
			// trying to code using this guileful language. The const cast is generally safe here
			// because the FConstInputHandle only allows const access to the internal node controller. 
			// Ostensibly, there could have been a INodeController and IConstNodeController
			// which take different types in their constructor, but it starts to become
			// difficult to maintain. So instead of adding 500 lines of nearly duplicate 
			// code, a ConstCastSharedRef is used here. 
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FConstInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		void FBaseNodeController::IterateOutputs(TUniqueFunction<void(FOutputHandle)> InFunction)
		{
			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, AsShared());
				if (OutputHandle->IsValid())
				{
					InFunction(OutputHandle);
				}
			}
		}

		const FText& FBaseNodeController::GetDisplayTitle() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Metadata.GetDisplayName();
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FBaseNodeController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return Class->Metadata.GetDisplayName();
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		void FBaseNodeController::IterateConstInputs(TUniqueFunction<void(FConstInputHandle)> InFunction) const
		{
			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParams())
			{
				FConstInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					InFunction(InputHandle);
				}
			}
		}

		TArray<FConstOutputHandle> FBaseNodeController::GetConstOutputs() const
		{
			TArray<FConstOutputHandle> Outputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		void FBaseNodeController::IterateConstOutputs(TUniqueFunction<void(FConstOutputHandle)> InFunction) const
		{
			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParams())
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					InFunction(OutputHandle);
				}
			}
		}

		TArray<FInputHandle> FBaseNodeController::GetInputsWithVertexName(const FString& InName) 
		{
			TArray<FInputHandle> Inputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FInputControllerParams& Params : GetInputControllerParamsWithVertexName(InName))
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		TArray<FConstInputHandle> FBaseNodeController::GetConstInputsWithVertexName(const FString& InName) const 
		{
			TArray<FConstInputHandle> Inputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FInputControllerParams& Params : GetInputControllerParamsWithVertexName(InName))
			{
				FInputHandle InputHandle = CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
				if (InputHandle->IsValid())
				{
					Inputs.Add(InputHandle);
				}
			}

			return Inputs;
		}

		TArray<FOutputHandle> FBaseNodeController::GetOutputsWithVertexName(const FString& InName)
		{
			TArray<FOutputHandle> Outputs;

			FNodeHandle ThisNode = this->AsShared();

			for (const FOutputControllerParams& Params : GetOutputControllerParamsWithVertexName(InName))
			{
				FOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		TArray<FConstOutputHandle> FBaseNodeController::GetConstOutputsWithVertexName(const FString& InName) const
		{
			TArray<FConstOutputHandle> Outputs;

			// See early use of ConstCastSharedRef in this class for discussion.
			FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());

			for (const FOutputControllerParams& Params : GetOutputControllerParamsWithVertexName(InName))
			{
				FConstOutputHandle OutputHandle = CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
				if (OutputHandle->IsValid())
				{
					Outputs.Add(OutputHandle);
				}
			}

			return Outputs;
		}

		FInputHandle FBaseNodeController::GetInputWithID(FGuid InVertexID)
		{
			FInputControllerParams Params;

			if (FindInputControllerParamsWithID(InVertexID, Params))
			{
				FNodeHandle ThisNode = this->AsShared();
				return CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
			}

			return FInvalidInputController::GetInvalid();
		}

		bool FBaseNodeController::IsRequired() const
		{
			return false;
		}

		FConstInputHandle FBaseNodeController::GetInputWithID(FGuid InVertexID) const
		{
			FInputControllerParams Params;

			if (FindInputControllerParamsWithID(InVertexID, Params))
			{
				// See early use of ConstCastSharedRef in this class for discussion.
				FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());
				return CreateInputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassInputPtr, ThisNode);
			}

			return FInvalidInputController::GetInvalid();
		}

		FOutputHandle FBaseNodeController::GetOutputWithID(FGuid InVertexID)
		{
			FOutputControllerParams Params;

			if (FindOutputControllerParamsWithID(InVertexID, Params))
			{
				FNodeHandle ThisNode = this->AsShared();
				return CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
			}

			return FInvalidOutputController::GetInvalid();
		}

		FConstOutputHandle FBaseNodeController::GetOutputWithID(FGuid InVertexID) const
		{
			FOutputControllerParams Params;

			if (FindOutputControllerParamsWithID(InVertexID, Params))
			{
				// See early use of ConstCastSharedRef in this class for discussion.
				FNodeHandle ThisNode = ConstCastSharedRef<INodeController>(this->AsShared());
				return CreateOutputController(Params.VertexID, Params.NodeVertexPtr, Params.ClassOutputPtr, ThisNode);
			}

			return FInvalidOutputController::GetInvalid();
		}

		TArray<FBaseNodeController::FInputControllerParams> FBaseNodeController::GetInputControllerParams() const
		{
			TArray<FBaseNodeController::FInputControllerParams> Inputs;

			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				for (const FMetasoundFrontendVertex& NodeInputVertex : Node->Interface.Inputs)
				{
					FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithName(NodeInputVertex.Name);
					FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(NodeInputVertex.Name);

					Inputs.Add({NodeInputVertex.VertexID, NodeVertexPtr, ClassInputPtr});
				}
			}

			return Inputs;
		}

		TArray<FBaseNodeController::FOutputControllerParams> FBaseNodeController::GetOutputControllerParams() const
		{
			TArray<FBaseNodeController::FOutputControllerParams> Outputs;

			if (const FMetasoundFrontendNode* Node = NodePtr.Get())
			{
				for (const FMetasoundFrontendVertex& NodeOutputVertex : Node->Interface.Outputs)
				{
					const FString& VertexName = NodeOutputVertex.Name;

					FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithName(VertexName);
					FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(VertexName);

					Outputs.Add({NodeOutputVertex.VertexID, NodeVertexPtr, ClassOutputPtr});
				}
			}

			return Outputs;
		}

		TArray<FBaseNodeController::FInputControllerParams> FBaseNodeController::GetInputControllerParamsWithVertexName(const FString& InName) const
		{
			TArray<FBaseNodeController::FInputControllerParams> Inputs;

			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithName(InName);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(InName);

				Inputs.Add({Vertex->VertexID, NodeVertexPtr, ClassInputPtr});
			}

			return Inputs;
		}

		TArray<FBaseNodeController::FOutputControllerParams> FBaseNodeController::GetOutputControllerParamsWithVertexName(const FString& InName) const
		{
			TArray<FBaseNodeController::FOutputControllerParams> Outputs;

			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithName(InName);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(InName);

				Outputs.Add({Vertex->VertexID, NodeVertexPtr, ClassOutputPtr});
			}

			return Outputs;
		}

		bool FBaseNodeController::FindInputControllerParamsWithID(FGuid InVertexID, FInputControllerParams& OutParams) const
		{
			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetInputWithVertexID(InVertexID);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassInputAccessPtr ClassInputPtr = ClassPtr.GetInputWithName(Vertex->Name);

				OutParams = FInputControllerParams{InVertexID, NodeVertexPtr, ClassInputPtr};
				return true;
			}

			return false;
		}

		bool FBaseNodeController::FindOutputControllerParamsWithID(FGuid InVertexID, FOutputControllerParams& OutParams) const
		{
			FConstVertexAccessPtr NodeVertexPtr = NodePtr.GetOutputWithVertexID(InVertexID);

			if (const FMetasoundFrontendVertex* Vertex = NodeVertexPtr.Get())
			{
				FConstClassOutputAccessPtr ClassOutputPtr = ClassPtr.GetOutputWithName(Vertex->Name);

				OutParams = FOutputControllerParams{InVertexID, NodeVertexPtr, ClassOutputPtr};
				return true;
			}

			return false;
		}

		FGraphHandle FBaseNodeController::AsGraph()
		{
			// TODO: consider adding support for external graph owned in another document.
			// Will require lookup support for external subgraphs..
			
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return GetOwningGraph()->GetOwningDocument()->GetSubgraphWithClassID(Class->ID);
			}

			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FBaseNodeController::AsGraph() const
		{
			// TODO: add support for graph owned in another asset.
			// Will require lookup support for external subgraphs.
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				return GetOwningGraph()->GetOwningDocument()->GetSubgraphWithClassID(Class->ID);
			}

			return FInvalidGraphController::GetInvalid();
		}

		FMetasoundFrontendVersionNumber FBaseNodeController::FindHighestMinorVersionInRegistry() const
		{
			const FMetasoundFrontendClassMetadata& Metadata = GetClassMetadata();
			const FMetasoundFrontendVersionNumber& CurrentVersion = Metadata.GetVersion();
			Metasound::FNodeClassName NodeClassName = Metadata.GetClassName().ToNodeClassName();

			FMetasoundFrontendClass ClassWithMajorVersion;
			if (ISearchEngine::Get().FindClassWithMajorVersion(NodeClassName, CurrentVersion.Major, ClassWithMajorVersion))
			{
				if (ClassWithMajorVersion.Metadata.GetVersion().Minor >= CurrentVersion.Minor)
				{
					return ClassWithMajorVersion.Metadata.GetVersion();
				}
			}

			return FMetasoundFrontendVersionNumber::GetInvalid();
		}

		FMetasoundFrontendVersionNumber FBaseNodeController::FindHighestVersionInRegistry() const
		{
			const FMetasoundFrontendClassMetadata& Metadata = GetClassMetadata();
			const FMetasoundFrontendVersionNumber& CurrentVersion = Metadata.GetVersion();
			Metasound::FNodeClassName NodeClassName = Metadata.GetClassName().ToNodeClassName();

			FMetasoundFrontendClass ClassWithHighestVersion;
			if (ISearchEngine::Get().FindClassWithHighestVersion(NodeClassName, ClassWithHighestVersion))
			{
				if (ClassWithHighestVersion.Metadata.GetVersion().Major >= CurrentVersion.Major)
				{
					return ClassWithHighestVersion.Metadata.GetVersion();
				}
			}

			return FMetasoundFrontendVersionNumber::GetInvalid();

		}

		FNodeHandle FBaseNodeController::ReplaceWithVersion(const FMetasoundFrontendVersionNumber& InNewVersion)
		{
			const FMetasoundFrontendClassMetadata& Metadata = GetClassMetadata();
			const TArray<FMetasoundFrontendClass> Versions = ISearchEngine::Get().FindClassesWithName(Metadata.GetClassName().ToNodeClassName(), false /* bInSortByVersion */);

			auto IsClassOfNewVersion = [InNewVersion](const FMetasoundFrontendClass& RegisteredClass)
			{
				return RegisteredClass.Metadata.GetVersion() == InNewVersion;
			};

			const FMetasoundFrontendClass* RegisteredClass = Versions.FindByPredicate(IsClassOfNewVersion);
			if (!ensure(RegisteredClass))
			{
				return this->AsShared();
			}

			FMetasoundFrontendNodeStyle Style = GetNodeStyle();

			using FConnectionKey = TPair<FString, FName>;

			struct FInputConnectionInfo
			{
				FOutputHandle ConnectedOutput;
				FName DataType;
				FMetasoundFrontendLiteral DefaultValue;
			};

			// Cache input/output connections by name to try so they can be
			// hooked back up after swapping to the new class version.
			TMap<FConnectionKey, FInputConnectionInfo> InputConnections;
			IterateInputs([Connections = &InputConnections](FInputHandle InputHandle)
			{
				FMetasoundFrontendLiteral DefaultLiteral;
				if (const FMetasoundFrontendLiteral* Literal = InputHandle->GetLiteral())
				{
					DefaultLiteral = *Literal;
				}
				else if (const FMetasoundFrontendLiteral* ClassLiteral = InputHandle->GetClassDefaultLiteral())
				{
					DefaultLiteral = *ClassLiteral;
				}

				const FConnectionKey ConnectionKey(InputHandle->GetName(), InputHandle->GetDataType());
				Connections->Add(ConnectionKey, FInputConnectionInfo
				{
					InputHandle->GetConnectedOutput(),
					InputHandle->GetDataType(),
					MoveTemp(DefaultLiteral)
				});
			});

			struct FOutputConnectionInfo
			{
				TArray<FInputHandle> ConnectedInputs;
				FName DataType;
			};

			TMap<FConnectionKey, FOutputConnectionInfo> OutputConnections;
			IterateOutputs([Connections = &OutputConnections](FOutputHandle OutputHandle)
			{
				const FConnectionKey ConnectionKey(OutputHandle->GetName(), OutputHandle->GetDataType());
				Connections->Add(ConnectionKey, FOutputConnectionInfo
				{
					OutputHandle->GetConnectedInputs(),
					OutputHandle->GetDataType(),
				});
			});

			if (!ensureAlways(GetOwningGraph()->RemoveNode(*this)))
			{
				return this->AsShared();
			}

			// Make sure classes are up-to-date with registered versions of class.
			// Note that this may break other nodes in the graph that have stale
			// class API, but that's on the caller to fix-up or report invalid state.
			const FNodeRegistryKey RegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(RegisteredClass->Metadata);
			FDocumentHandle Document = GetOwningGraph()->GetOwningDocument();
			ensureAlways(Document->SynchronizeDependency(RegistryKey) != nullptr);

			FNodeHandle ReplacementNode = GetOwningGraph()->AddNode(RegisteredClass->Metadata);
			if (!ensureAlways(ReplacementNode->IsValid()))
			{
				return this->AsShared();
			}

			ReplacementNode->SetNodeStyle(Style);

			ReplacementNode->IterateInputs([Connections = &InputConnections](FInputHandle InputHandle)
			{
				const FConnectionKey ConnectionKey(InputHandle->GetName(), InputHandle->GetDataType());
				if (FInputConnectionInfo* ConnectionInfo = Connections->Find(ConnectionKey))
				{
					InputHandle->SetLiteral(ConnectionInfo->DefaultValue);
					if (ConnectionInfo->ConnectedOutput->IsValid())
					{
						ensure(InputHandle->Connect(*ConnectionInfo->ConnectedOutput));
					}
				}
			});

			ReplacementNode->IterateOutputs([Connections = &OutputConnections](FOutputHandle OutputHandle)
			{
				const FConnectionKey ConnectionKey(OutputHandle->GetName(), OutputHandle->GetDataType());
				if (FOutputConnectionInfo* ConnectionInfo = Connections->Find(ConnectionKey))
				{
					for (FInputHandle InputHandle : ConnectionInfo->ConnectedInputs)
					{
						if (InputHandle->IsValid())
						{
							ensure(InputHandle->Connect(*OutputHandle));
						}
					}
				}
			});

			return ReplacementNode;
		}

		bool FBaseNodeController::DiffAgainstRegistryInterface(FClassInterfaceUpdates& OutInterfaceUpdates, bool bInUseHighestMinorVersion) const
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(BaseNodeController::DiffAgainstRegistryInterface);

			OutInterfaceUpdates = FClassInterfaceUpdates();

			const FMetasoundFrontendClassMetadata& NodeClassMetadata = GetClassMetadata();
			const FMetasoundFrontendClassInterface& NodeClassInterface = GetClassInterface();

			Metasound::FNodeClassName NodeClassName = NodeClassMetadata.GetClassName().ToNodeClassName();

			if (bInUseHighestMinorVersion)
			{
				if (!ISearchEngine::Get().FindClassWithMajorVersion(NodeClassName, NodeClassMetadata.GetVersion().Major, OutInterfaceUpdates.RegistryClass))
				{
					Algo::Transform(NodeClassInterface.Inputs, OutInterfaceUpdates.RemovedInputs, [&](const FMetasoundFrontendClassInput& Input) { return &Input; });
					Algo::Transform(NodeClassInterface.Outputs, OutInterfaceUpdates.RemovedOutputs, [&](const FMetasoundFrontendClassOutput& Output) { return &Output; });
					return false;
				}
			}
			else
			{
				constexpr bool bSortByVersion = true;
				const TArray<FMetasoundFrontendClass> Classes = ISearchEngine::Get().FindClassesWithName(NodeClassName, bSortByVersion);
				const FMetasoundFrontendClass* ExactClass = Classes.FindByPredicate([CurrentVersion = &NodeClassMetadata.GetVersion()](const FMetasoundFrontendClass& AvailableClass)
				{
					return AvailableClass.Metadata.GetVersion() == *CurrentVersion;
				});

				if (!ExactClass)
				{
					Algo::Transform(NodeClassInterface.Inputs, OutInterfaceUpdates.RemovedInputs, [&](const FMetasoundFrontendClassInput& Input) { return &Input; });
					Algo::Transform(NodeClassInterface.Outputs, OutInterfaceUpdates.RemovedOutputs, [&](const FMetasoundFrontendClassOutput& Output) { return &Output; });
					return false;
				}
				OutInterfaceUpdates.RegistryClass = *ExactClass;
			}

			Algo::Transform(OutInterfaceUpdates.RegistryClass.Interface.Inputs, OutInterfaceUpdates.AddedInputs, [&](const FMetasoundFrontendClassInput& Input) { return &Input; });
			for (const FMetasoundFrontendClassInput& Input : NodeClassInterface.Inputs)
			{
				auto IsFunctionalEquivalent = [NodeClassInput = &Input](const FMetasoundFrontendClassInput* Iter)
				{
					return FMetasoundFrontendClassVertex::IsFunctionalEquivalent(*NodeClassInput, *Iter);
				};

				const int32 Index = OutInterfaceUpdates.AddedInputs.FindLastByPredicate(IsFunctionalEquivalent);
				if (Index == INDEX_NONE)
				{
					OutInterfaceUpdates.RemovedInputs.Add(&Input);
				}
				else
				{
					OutInterfaceUpdates.AddedInputs.RemoveAtSwap(Index, 1, false /* bAllowShrinking */);
				}
			}

			Algo::Transform(OutInterfaceUpdates.RegistryClass.Interface.Outputs, OutInterfaceUpdates.AddedOutputs, [&](const FMetasoundFrontendClassOutput& Output) { return &Output; });
			for (const FMetasoundFrontendClassOutput& Output : NodeClassInterface.Outputs)
			{
				auto IsFunctionalEquivalent = [NodeClassOutput = &Output](const FMetasoundFrontendClassOutput* Iter)
				{
					return FMetasoundFrontendClassVertex::IsFunctionalEquivalent(*NodeClassOutput, *Iter);
				};

				const int32 Index = OutInterfaceUpdates.AddedOutputs.FindLastByPredicate(IsFunctionalEquivalent);
				if (Index == INDEX_NONE)
				{
					OutInterfaceUpdates.RemovedOutputs.Add(&Output);
				}
				else
				{
					OutInterfaceUpdates.AddedOutputs.RemoveAtSwap(Index, 1, false /* bAllowShrinking */);
				}
			}

			return true;
		}

		bool FBaseNodeController::CanAutoUpdate(FClassInterfaceUpdates* OutInterfaceUpdates) const
		{
			METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(BaseNodeController::CanAutoUpdate);

			const FMetasoundFrontendClassMetadata& NodeClassMetadata = GetClassMetadata();
			if (!IMetaSoundAssetManager::GetChecked().CanAutoUpdate(NodeClassMetadata.GetClassName()))
			{
				return false;
			}

			FMetasoundFrontendClass RegistryClass;
			if (!ISearchEngine::Get().FindClassWithMajorVersion(
				NodeClassMetadata.GetClassName().ToNodeClassName(),
				NodeClassMetadata.GetVersion().Major,
				RegistryClass))
			{
				return false;
			}

			if (RegistryClass.Metadata.GetVersion() < NodeClassMetadata.GetVersion())
			{
				return false;
			}

			if (RegistryClass.Metadata.GetVersion() == NodeClassMetadata.GetVersion())
			{
				// TODO: Merge these paths.  Shouldn't use different logic to
				// define changes in native vs asset class definitions.
				const FNodeRegistryKey RegistryKey = NodeRegistryKey::CreateKey(RegistryClass.Metadata);
				const bool bIsClassNative = FMetasoundFrontendRegistryContainer::Get()->IsNodeNative(RegistryKey);
				if (bIsClassNative)
				{
					if (!MetaSoundAutoUpdateNativeClassCVar)
					{
						return false;
					}

					FClassInterfaceUpdates InterfaceUpdates;
					DiffAgainstRegistryInterface(InterfaceUpdates, true /* bUseHighestMinorVersion */);
					if (OutInterfaceUpdates)
					{
						*OutInterfaceUpdates = InterfaceUpdates;
					}

					if (!InterfaceUpdates.ContainsChanges())
					{
						return false;
					}
				}
				else
				{
					if (RegistryClass.Metadata.GetChangeID() == NodeClassMetadata.GetChangeID())
					{
						const FGuid& NodeClassInterfaceChangeID = GetClassInterface().GetChangeID();
						if (RegistryClass.Interface.GetChangeID() == NodeClassInterfaceChangeID)
						{
							return false;
						}
					}
				}
			}

			return true;
		}

		FDocumentAccess FBaseNodeController::ShareAccess()
		{
			FDocumentAccess Access;

			Access.Node = NodePtr;
			Access.ConstNode = NodePtr;
			Access.ConstClass = ClassPtr;

			return Access;
		}

		FConstDocumentAccess FBaseNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstNode = NodePtr;
			Access.ConstClass = ClassPtr;

			return Access;
		}


		// 
		// FNodeController
		//
		FNodeController::FNodeController(EPrivateToken InToken, const FNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, GraphPtr(InParams.GraphPtr)
		{
		}

		FNodeHandle FNodeController::CreateNodeHandle(const FNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					// Cannot make a valid node handle if the node description and class description differ
					if (Node->ClassID == Class->ID)
					{
						return MakeShared<FNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
					}
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FNodeController::CreateConstNodeHandle(const FNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					// Cannot make a valid node handle if the node description and class description differ
					if (Node->ClassID == Class->ID)
					{
						return MakeShared<const FNodeController>(EPrivateToken::Token, InParams);
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
					}
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		bool FNodeController::IsValid() const
		{
			return FBaseNodeController::IsValid() && (nullptr != GraphPtr.Get());
		}

		FInputHandle FNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseInputController>(FBaseInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FBaseOutputController>(FBaseOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, GraphPtr, InOwningNode});
		}

		FDocumentAccess FNodeController::ShareAccess() 
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;

			return Access;
		}

		FConstDocumentAccess FNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;

			return Access;
		}


		//
		// FOutputNodeController
		//
		FOutputNodeController::FOutputNodeController(FOutputNodeController::EPrivateToken InToken, const FOutputNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, GraphPtr(InParams.GraphPtr)
		, OwningGraphClassOutputPtr(InParams.OwningGraphClassOutputPtr)
		{
		}

		FNodeHandle FOutputNodeController::CreateOutputNodeHandle(const FOutputNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					if (EMetasoundFrontendClassType::Output == Class->Metadata.GetType())
					{
						if (Class->ID == Node->ClassID)
						{
							return MakeShared<FOutputNodeController>(EPrivateToken::Token, InParams);
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating output node.. Must be EMetasoundFrontendClassType::Output."), *Class->ID.ToString());
					}
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		const FText& FOutputNodeController::GetDescription() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FOutputNodeController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassOutput* OwningOutput = OwningGraphClassOutputPtr.Get())
			{
				return OwningOutput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		void FOutputNodeController::SetDescription(const FText& InDescription)
		{
			// TODO: can we remove the const cast by constructing output nodes with a non-const access to class outputs?
			if (FMetasoundFrontendClassOutput* ClassOutput = ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr).Get())
			{
				ClassOutput->Metadata.Description = InDescription;
				OwningGraph->UpdateInterfaceChangeID();
			}
		}

		void FOutputNodeController::SetDisplayName(const FText& InDisplayName)
		{
			// TODO: can we remove the const cast by constructing output nodes with a non-const access to class outputs?
			if (FMetasoundFrontendClassOutput* ClassOutput = ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr).Get())
			{
				ClassOutput->Metadata.DisplayName = InDisplayName;
				OwningGraph->UpdateInterfaceChangeID();
			}
		}

		FConstNodeHandle FOutputNodeController::CreateConstOutputNodeHandle(const FOutputNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					if (EMetasoundFrontendClassType::Output == Class->Metadata.GetType())
					{
						if (Class->ID == Node->ClassID)
						{
							return MakeShared<const FOutputNodeController>(EPrivateToken::Token, InParams);
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating output node.. Must be EMetasoundFrontendClassType::Output."), *Class->ID.ToString());
					}
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		const FText& FOutputNodeController::GetDisplayTitle() const
		{
			static FText OutputDisplayTitle = LOCTEXT("OutputNode_Title", "Output");
			return OutputDisplayTitle;
		}

		bool FOutputNodeController::IsRequired() const
		{
			FConstDocumentHandle OwningDocument = OwningGraph->GetOwningDocument();
			FConstGraphHandle RootGraph = OwningDocument->GetRootGraph();

			// Test if this node exists on the document's root graph.
			const bool bIsNodeOnRootGraph = OwningGraph->IsValid() && (RootGraph->GetClassID() == OwningGraph->GetClassID());

			if (bIsNodeOnRootGraph)
			{
				// If the node is on the root graph, test if it is in the archetypes
				// required inputs or outputs. 
				FMetasoundFrontendArchetype Archetype;

				FArchetypeRegistryKey ArchetypeKey = GetArchetypeRegistryKey(OwningDocument->GetArchetypeVersion());

				bool bFoundArchetype = IArchetypeRegistry::Get().FindArchetype(ArchetypeKey, Archetype);
				if (bFoundArchetype)
				{
					if (const FMetasoundFrontendNode* Node = NodePtr.Get())
					{
						const FString& Name = Node->Name;
						auto IsVertexWithSameName = [&Name](const FMetasoundFrontendClassVertex& InVertex)
						{
							return InVertex.Name == Name;
						};
						return Archetype.Interface.Outputs.ContainsByPredicate(IsVertexWithSameName);
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Document using unregistered archetype [ArchetypeVersion:%s]"), *OwningDocument->GetArchetypeVersion().ToString());
				}
			}

			return false;
		}

		bool FOutputNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && (nullptr != OwningGraphClassOutputPtr.Get()) && (nullptr != GraphPtr.Get());
		}

		FInputHandle FOutputNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FOutputNodeInputController>(FOutputNodeInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, OwningGraphClassOutputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FOutputNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FOutputNodeOutputController>(FOutputNodeOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, OwningGraphClassOutputPtr, GraphPtr, InOwningNode});
		}

		FDocumentAccess FOutputNodeController::ShareAccess()
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;
			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}

		FConstDocumentAccess FOutputNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;
			Access.ConstClassOutput = OwningGraphClassOutputPtr;

			return Access;
		}


		//
		// FInputNodeController
		//
		FInputNodeController::FInputNodeController(EPrivateToken InToken, const FInputNodeController::FInitParams& InParams)
		: FBaseNodeController({InParams.NodePtr, InParams.ClassPtr, InParams.OwningGraph})
		, OwningGraphClassInputPtr(InParams.OwningGraphClassInputPtr)
		, GraphPtr(InParams.GraphPtr)
		{
		}

		FNodeHandle FInputNodeController::CreateInputNodeHandle(const FInputNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					if (EMetasoundFrontendClassType::Input == Class->Metadata.GetType())
					{
						if (Class->ID == Node->ClassID)
						{
							return MakeShared<FInputNodeController>(EPrivateToken::Token, InParams);
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating input node. Must be EMetasoundFrontendClassType::Input."), *Class->ID.ToString());
					}
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FInputNodeController::CreateConstInputNodeHandle(const FInputNodeController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendNode* Node = InParams.NodePtr.Get())
			{
				if (const FMetasoundFrontendClass* Class = InParams.ClassPtr.Get())
				{
					if (EMetasoundFrontendClassType::Input == Class->Metadata.GetType())
					{
						if (Class->ID == Node->ClassID)
						{
							return MakeShared<const FInputNodeController>(EPrivateToken::Token, InParams);
						}
						else
						{
							UE_LOG(LogMetaSound, Warning, TEXT("Frontend Node [NodeID:%s, ClassID:%s] is not of expected class class [ClassID:%s]"), *Node->ID.ToString(), *Node->ClassID.ToString(), *Class->ID.ToString());
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Frontend Class of incorrect class type [ClassID:%s] while creating input node. Must be EMetasoundFrontendClassType::Input."), *Class->ID.ToString());
					}
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FInputNodeController::IsValid() const
		{
			return OwningGraph->IsValid() && (nullptr != OwningGraphClassInputPtr.Get()) && (nullptr != GraphPtr.Get());
		}

		FInputHandle FInputNodeController::CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FInputNodeInputController>(FInputNodeInputController::FInitParams{InVertexID, InNodeVertexPtr, InClassInputPtr, OwningGraphClassInputPtr, GraphPtr, InOwningNode});
		}

		FOutputHandle FInputNodeController::CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const
		{
			return MakeShared<FInputNodeOutputController>(FInputNodeOutputController::FInitParams{InVertexID, InNodeVertexPtr, InClassOutputPtr, OwningGraphClassInputPtr, GraphPtr, InOwningNode});
		}

		const FText& FInputNodeController::GetDescription() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.Description;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeController::GetDisplayName() const
		{
			if (const FMetasoundFrontendClassInput* OwningInput = OwningGraphClassInputPtr.Get())
			{
				return OwningInput->Metadata.DisplayName;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		const FText& FInputNodeController::GetDisplayTitle() const
		{
			static FText InputDisplayTitle = LOCTEXT("InputNode_Title", "Input");
			return InputDisplayTitle;
		}

		bool FInputNodeController::IsRequired() const
		{
			FConstDocumentHandle OwningDocument = OwningGraph->GetOwningDocument();
			FConstGraphHandle RootGraph = OwningDocument->GetRootGraph();

			// Test if this node exists on the document's root graph.
			const bool bIsNodeOnRootGraph = OwningGraph->IsValid() && (RootGraph->GetClassID() == OwningGraph->GetClassID());

			if (bIsNodeOnRootGraph)
			{
				// If the node is on the root graph, test if it is in the archetypes
				// required inputs or outputs. 
				FMetasoundFrontendArchetype Archetype;

				FArchetypeRegistryKey ArchetypeKey = GetArchetypeRegistryKey(OwningDocument->GetArchetypeVersion());

				bool bFoundArchetype = IArchetypeRegistry::Get().FindArchetype(ArchetypeKey, Archetype);
				if (bFoundArchetype)
				{
					if (const FMetasoundFrontendNode* Node = NodePtr.Get())
					{
						const FString& Name = Node->Name;
						auto IsVertexWithSameName = [&Name](const FMetasoundFrontendClassVertex& InVertex)
						{
							return InVertex.Name == Name;
						};
						return Archetype.Interface.Inputs.ContainsByPredicate(IsVertexWithSameName);
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Document using unregistered archetype [ArchetypeVersion:%s]"), *OwningDocument->GetArchetypeVersion().ToString());
				}
			}

			return false;
		}

		void FInputNodeController::SetDescription(const FText& InDescription)
		{
			// TODO: can we remove these const casts by constructing FINputNodeController with non-const access to the class input?
			if (FMetasoundFrontendClassInput* ClassInput = ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr).Get())
			{
				ClassInput->Metadata.Description = InDescription;
				OwningGraph->UpdateInterfaceChangeID();
			}
		}

		void FInputNodeController::SetDisplayName(const FText& InDisplayName)
		{
			// TODO: can we remove these const casts by constructing FINputNodeController with non-const access to the class input?
			if (FMetasoundFrontendClassInput* ClassInput = ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr).Get())
			{
				ClassInput->Metadata.DisplayName = InDisplayName;
				OwningGraph->UpdateInterfaceChangeID();
			}
		}

		FDocumentAccess FInputNodeController::ShareAccess()
		{
			FDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.Graph = GraphPtr;
			Access.ConstGraph = GraphPtr;
			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		FConstDocumentAccess FInputNodeController::ShareAccess() const
		{
			FConstDocumentAccess Access = FBaseNodeController::ShareAccess();

			Access.ConstGraph = GraphPtr;
			Access.ConstClassInput = OwningGraphClassInputPtr;

			return Access;
		}

		// 
		// FGraphController
		//
		FGraphController::FGraphController(EPrivateToken InToken, const FGraphController::FInitParams& InParams)
		: GraphClassPtr(InParams.GraphClassPtr)
		, OwningDocument(InParams.OwningDocument)
		{
		}

		FGraphHandle FGraphController::CreateGraphHandle(const FGraphController::FInitParams& InParams)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = InParams.GraphClassPtr.Get())
			{
				if (GraphClass->Metadata.GetType() == EMetasoundFrontendClassType::Graph)
				{
					return MakeShared<FGraphController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to make graph controller [ClassID:%s]. Class must be EMeatsoundFrontendClassType::Graph."), *GraphClass->ID.ToString())
				}
			}
			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FGraphController::CreateConstGraphHandle(const FGraphController::FInitParams& InParams)
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = InParams.GraphClassPtr.Get())
			{
				if (GraphClass->Metadata.GetType() == EMetasoundFrontendClassType::Graph)
				{
					return MakeShared<FGraphController>(EPrivateToken::Token, InParams);
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Failed to make graph controller [ClassID:%s]. Class must be EMeatsoundFrontendClassType::Graph."), *GraphClass->ID.ToString())
				}
			}
			return FInvalidGraphController::GetInvalid();
		}

		bool FGraphController::IsValid() const
		{
			return (nullptr != GraphClassPtr.Get()) && OwningDocument->IsValid();
		}

		FGuid FGraphController::GetClassID() const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->ID;
			}

			return Metasound::FrontendInvalidID;
		}

		const FText& FGraphController::GetDisplayName() const 
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Metadata.GetDisplayName();
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FText>();
		}

		TArray<FString> FGraphController::GetInputVertexNames() const 
		{
			TArray<FString> Names;

			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendClassInput& Input : GraphClass->Interface.Inputs)
				{
					Names.Add(Input.Name);
				}
			}

			return Names;
		}

		TArray<FString> FGraphController::GetOutputVertexNames() const
		{
			TArray<FString> Names;

			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendClassOutput& Output : GraphClass->Interface.Outputs)
				{
					Names.Add(Output.Name);
				}
			}

			return Names;
		}

		FConstClassInputAccessPtr FGraphController::FindClassInputWithName(const FString& InName) const
		{
			return GraphClassPtr.GetInputWithName(InName);
		}

		FConstClassOutputAccessPtr FGraphController::FindClassOutputWithName(const FString& InName) const
		{
			return GraphClassPtr.GetOutputWithName(InName);
		}

		FGuid FGraphController::GetVertexIDForInputVertex(const FString& InInputName) const
		{
			if (const FMetasoundFrontendClassInput* Input = FindClassInputWithName(InInputName).Get())
			{
				return Input->VertexID;
			}
			return Metasound::FrontendInvalidID;
		}

		FGuid FGraphController::GetVertexIDForOutputVertex(const FString& InOutputName) const
		{
			if (const FMetasoundFrontendClassOutput* Output = FindClassOutputWithName(InOutputName).Get())
			{
				return Output->VertexID;
			}
			return Metasound::FrontendInvalidID;
		}

		TArray<FNodeHandle> FGraphController::GetNodes()
		{
			return GetNodeHandles(GetNodesAndClasses());
		}

		TArray<FConstNodeHandle> FGraphController::GetConstNodes() const
		{
			return GetNodeHandles(GetNodesAndClasses());
		}

		FConstNodeHandle FGraphController::GetNodeWithID(FGuid InNodeID) const
		{
			auto IsNodeWithSameID = [&](const FMetasoundFrontendClass& NodeClas, const FMetasoundFrontendNode& Node) 
			{ 
				return Node.ID == InNodeID; 
			};
			return GetNodeByPredicate(IsNodeWithSameID);
		}

		FNodeHandle FGraphController::GetNodeWithID(FGuid InNodeID)
		{
			auto IsNodeWithSameID = [&](const FMetasoundFrontendClass& NodeClas, const FMetasoundFrontendNode& Node) 
			{ 
				return Node.ID == InNodeID; 
			};
			return GetNodeByPredicate(IsNodeWithSameID);
		}

		const FMetasoundFrontendGraphStyle& FGraphController::GetGraphStyle() const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Graph.Style;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendGraphStyle>();
		}

		void FGraphController::SetGraphStyle(const FMetasoundFrontendGraphStyle& InStyle)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				GraphClass->Graph.Style = InStyle;
			}
		}

		TArray<FNodeHandle> FGraphController::GetOutputNodes()
		{
			auto IsOutputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Output;
			};
			return GetNodesByPredicate(IsOutputNode);
		}

		TArray<FNodeHandle> FGraphController::GetInputNodes()
		{
			auto IsInputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node)
			{
				return NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Input;
			};
			return GetNodesByPredicate(IsInputNode);
		}

		void FGraphController::ClearGraph()
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				GraphClass->Graph.Nodes.Reset();
				GraphClass->Graph.Edges.Reset();
				GraphClass->Interface.Inputs.Reset();
				GraphClass->Interface.Outputs.Reset();
				OwningDocument->SynchronizeDependencies();
			}
		}

		void FGraphController::IterateNodes(TUniqueFunction<void(FNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InClassType == EMetasoundFrontendClassType::Invalid || NodeClass->Metadata.GetType() == InClassType)
						{
							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							FNodeHandle NodeHandle = GetNodeHandle(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
							InFunction(NodeHandle);
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}
		}

		void FGraphController::IterateConstNodes(TUniqueFunction<void(FConstNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType) const
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InClassType == EMetasoundFrontendClassType::Invalid || NodeClass->Metadata.GetType() == InClassType)
						{
							FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							FConstNodeHandle NodeHandle = GetNodeHandle(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
							InFunction(NodeHandle);
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}
		}

		TArray<FConstNodeHandle> FGraphController::GetConstOutputNodes() const
		{
			auto IsOutputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Output;
			};
			return GetNodesByPredicate(IsOutputNode);
		}

		TArray<FConstNodeHandle> FGraphController::GetConstInputNodes() const
		{
			auto IsInputNode = [](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{
				return NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Input;
			};
			return GetNodesByPredicate(IsInputNode);
		}

		bool FGraphController::ContainsOutputVertexWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsOutputVertexWithSameName = [&](const FMetasoundFrontendClassOutput& ClassOutput)
				{
					return ClassOutput.Name == InName;
				};
				return GraphClass->Interface.Outputs.ContainsByPredicate(IsOutputVertexWithSameName);
			}
			return false;
		}

		bool FGraphController::ContainsInputVertexWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsInputVertexWithSameName = [&](const FMetasoundFrontendClassInput& ClassInput) 
				{ 
					return ClassInput.Name == InName;
				};
				return GraphClass->Interface.Inputs.ContainsByPredicate(IsInputVertexWithSameName);
			}
			return false;
		}

		FConstNodeHandle FGraphController::GetOutputNodeWithName(const FString& InName) const
		{
			auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Output) && (Node.Name == InName);
			};
			return GetNodeByPredicate(IsOutputNodeWithSameName);
		}

		FConstNodeHandle FGraphController::GetInputNodeWithName(const FString& InName) const
		{
			auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Input) && (Node.Name == InName);
			};
			return GetNodeByPredicate(IsInputNodeWithSameName);
		}

		FNodeHandle FGraphController::GetOutputNodeWithName(const FString& InName)
		{
			auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node) 
			{ 
				return (NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Output) && (Node.Name == InName);
			};
			return GetNodeByPredicate(IsOutputNodeWithSameName);
		}

		FNodeHandle FGraphController::GetInputNodeWithName(const FString& InName)
		{
			auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& NodeClass, const FMetasoundFrontendNode& Node)
			{
				return (NodeClass.Metadata.GetType() == EMetasoundFrontendClassType::Input) && (Node.Name == InName);
			};
			return GetNodeByPredicate(IsInputNodeWithSameName);
		}

		FNodeHandle FGraphController::AddInputVertex(const FMetasoundFrontendClassInput& InClassInput)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsInputWithSameName = [&](const FMetasoundFrontendClassInput& ExistingDesc) { return ExistingDesc.Name == InClassInput.Name; };
				if (Algo::NoneOf(GraphClass->Interface.Inputs, IsInputWithSameName))
				{
					FNodeRegistryKey Key;
					if (FRegistry::GetInputNodeRegistryKeyForDataType(InClassInput.TypeName, Key))
					{
						FConstClassAccessPtr InputClassPtr = OwningDocument->FindOrAddClass(Key);
						if (const FMetasoundFrontendClass* InputClass = InputClassPtr.Get())
						{
							FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*InputClass);

							Node.Name = InClassInput.Name;
							Node.ID = FGuid::NewGuid();

							auto IsVertexWithTypeName = [&](FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InClassInput.TypeName; };
							if (FMetasoundFrontendVertex* InputVertex = Node.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								InputVertex->Name = InClassInput.Name;
							}
							else
							{
								UE_LOG(LogMetaSound, Error, TEXT("Input node [TypeName:%s] does not contain input vertex with type [TypeName:%s]"), *InClassInput.TypeName.ToString(), *InClassInput.TypeName.ToString());
							}

							if (Node.Interface.Outputs.Num() == 1)
							{
								Node.Interface.Outputs[0].Name = InClassInput.Name;
							}
							else if (FMetasoundFrontendVertex* OutputVertex = Node.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								OutputVertex->Name = InClassInput.Name;
							}

							FMetasoundFrontendClassInput& NewInput = GraphClass->Interface.Inputs.Add_GetRef(InClassInput);
							NewInput.NodeID = Node.ID;

							GraphClass->Interface.UpdateChangeID();

							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							return GetNodeHandle(FGraphController::FNodeAndClass{NodePtr, InputClassPtr});
						}
					}
					else 
					{
						UE_LOG(LogMetaSound, Display, TEXT("Failed to add input. No input node registered for data type [TypeName:%s]"), *InClassInput.TypeName.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Display, TEXT("Failed to add input. Input with same name \"%s\" exists in class [ClassID:%s]"), *InClassInput.Name, *GraphClass->ID.ToString());
				}
			}
			return FInvalidNodeController::GetInvalid();
		}

		FNodeHandle FGraphController::AddInputNode(const FMetasoundFrontendClassInput& InClassInput, const FMetasoundFrontendLiteral* InDefaultValue, const FText* InDisplayName)
		{
			FNodeHandle NodeHandle = AddInputVertex(InClassInput);
			if (ensure(NodeHandle->IsValid()))
			{
				if (InDisplayName)
				{
					SetInputDisplayName(InClassInput.Name, *InDisplayName);
				}

				if (InDefaultValue)
				{
					SetDefaultInput(InClassInput.VertexID, *InDefaultValue);
				}
				else
				{
					SetDefaultInputToDefaultLiteralOfType(InClassInput.VertexID);
				}
			}

			return NodeHandle;
		}

		FNodeHandle FGraphController::AddInputNode(const FName InTypeName, const FText& InToolTip, const FMetasoundFrontendLiteral* InDefaultValue, const FText* InDisplayName)
		{
			const FGuid VertexID = FGuid::NewGuid();

			FMetasoundFrontendClassInput Description;

			Description.Name = VertexID.ToString();
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = VertexID;

			return AddInputNode(Description, InDefaultValue, InDisplayName);
		}

		FNodeHandle FGraphController::AddOutputNode(const FMetasoundFrontendClassOutput& InClassOutput, const FText* InDisplayName)
		{
			FNodeHandle NewOutput = AddOutputVertex(InClassOutput);
			if (ensure(NewOutput->IsValid()))
			{
				if (InDisplayName)
				{
					SetOutputDisplayName(InClassOutput.Name, *InDisplayName);
				}
			}

			return NewOutput;
		}

		FNodeHandle FGraphController::AddOutputNode(const FName InTypeName, const FText& InToolTip, const FText* InDisplayName)
		{
			const FGuid VertexID = FGuid::NewGuid();

			FMetasoundFrontendClassOutput Description;

			Description.Name = VertexID.ToString();
			Description.TypeName = InTypeName;
			Description.Metadata.Description = InToolTip;
			Description.VertexID = VertexID;

			return AddOutputNode(Description, InDisplayName);
		}


		bool FGraphController::RemoveInputVertex(const FString& InName)
		{
			auto IsInputNodeWithSameName = [&](const FMetasoundFrontendClass& InClass, const FMetasoundFrontendNode& InNode)
			{
				return (InClass.Metadata.GetType() == EMetasoundFrontendClassType::Input) && (InNode.Name == InName);
			};

			for (const FNodeAndClass& NodeAndClass : GetNodesAndClassesByPredicate(IsInputNodeWithSameName))
			{
				if (const FMetasoundFrontendNode* Node = NodeAndClass.Node.Get())
				{
					return RemoveInput(*Node);
				}
			}

			return false;
		}

		FNodeHandle FGraphController::AddOutputVertex(const FMetasoundFrontendClassOutput& InClassOutput)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsOutputWithSameName = [&](const FMetasoundFrontendClassOutput& ExistingDesc) { return ExistingDesc.Name == InClassOutput.Name; };
				if (Algo::NoneOf(GraphClass->Interface.Outputs, IsOutputWithSameName))
				{
					FNodeRegistryKey Key;
					if (FRegistry::GetOutputNodeRegistryKeyForDataType(InClassOutput.TypeName, Key))
					{
						FConstClassAccessPtr OutputClassPtr = OwningDocument->FindOrAddClass(Key);
						if (const FMetasoundFrontendClass* OutputClass = OutputClassPtr.Get())
						{
							FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*OutputClass);

							Node.Name = InClassOutput.Name;
							Node.ID = FGuid::NewGuid();

							// TODO: have something that checks if input node has valid interface.
							auto IsVertexWithTypeName = [&](FMetasoundFrontendVertex& Vertex) { return Vertex.TypeName == InClassOutput.TypeName; };
							if (Node.Interface.Inputs.Num() == 1)
							{
								Node.Interface.Inputs[0].Name = InClassOutput.Name;
							}
							else if (FMetasoundFrontendVertex* InputVertex = Node.Interface.Inputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								InputVertex->Name = InClassOutput.Name;
							}

							if (FMetasoundFrontendVertex* OutputVertex = Node.Interface.Outputs.FindByPredicate(IsVertexWithTypeName))
							{ 
								OutputVertex->Name = InClassOutput.Name;
							}
							else
							{
								UE_LOG(LogMetaSound, Error, TEXT("Output node [TypeName:%s] does not contain output vertex with type [TypeName:%s]"), *InClassOutput.TypeName.ToString(), *InClassOutput.TypeName.ToString());
							}

							FMetasoundFrontendClassOutput& NewOutput = GraphClass->Interface.Outputs.Add_GetRef(InClassOutput);
							NewOutput.NodeID = Node.ID;

							GraphClass->Interface.UpdateChangeID();

							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							return GetNodeHandle(FGraphController::FNodeAndClass { NodePtr, OutputClassPtr });
						}
					}
					else 
					{
						UE_LOG(LogMetaSound, Display, TEXT("Failed to add output. No output node registered for data type [TypeName:%s]"), *InClassOutput.TypeName.ToString());
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Display, TEXT("Failed to add output. Output with same name \"%s\" exists in class [ClassID:%s]"), *InClassOutput.Name, *GraphClass->ID.ToString());
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FGraphController::RemoveOutputVertex(const FString& InName)
		{
			auto IsOutputNodeWithSameName = [&](const FMetasoundFrontendClass& InClass, const FMetasoundFrontendNode& InNode)
			{
				return (InClass.Metadata.GetType() == EMetasoundFrontendClassType::Output) && (InNode.Name == InName);
			};

			for (const FNodeAndClass& NodeAndClass : GetNodesAndClassesByPredicate(IsOutputNodeWithSameName))
			{
				if (const FMetasoundFrontendNode* Node = NodeAndClass.Node.Get())
				{
					return RemoveOutput(*Node);
				}
			}

			return false;
		}

		// This can be used to determine what kind of property editor we should use for the data type of a given input.
		// Will return Invalid if the input couldn't be found, or if the input doesn't support any kind of literals.
		ELiteralType FGraphController::GetPreferredLiteralTypeForInputVertex(const FString& InInputName) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				return FRegistry::Get()->GetDesiredLiteralTypeForDataType(Desc->TypeName);
			}
			return ELiteralType::Invalid;
		}

		// For inputs whose preferred literal type is UObject or UObjectArray, this can be used to determine the UObject corresponding to that input's datatype.
		UClass* FGraphController::GetSupportedClassForInputVertex(const FString& InInputName)
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				return FRegistry::Get()->GetLiteralUClassForDataType(Desc->TypeName);
			}
			return nullptr;
		}

		FMetasoundFrontendLiteral FGraphController::GetDefaultInput(const FGuid& InVertexID) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				return Desc->DefaultLiteral;
			}
			return FMetasoundFrontendLiteral{};
		}

		bool FGraphController::SetDefaultInput(const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				if (ensure(DoesDataTypeSupportLiteralType(Desc->TypeName, InLiteral.GetType())))
				{
					Desc->DefaultLiteral = InLiteral;
					return true;
				}
				else
				{
					SetDefaultInputToDefaultLiteralOfType(InVertexID);
				}
			}

			return false;
		}

		bool FGraphController::SetDefaultInputToDefaultLiteralOfType(const FGuid& InVertexID)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithVertexID(InVertexID))
			{
				Metasound::FLiteral Literal = Frontend::GetDefaultParamForDataType(Desc->TypeName);
				Desc->DefaultLiteral.SetFromLiteral(Literal);
				return Desc->DefaultLiteral.IsValid();
			}

			return false;
		}

		const FText& FGraphController::GetInputDescription(const FString& InName) const
		{
			if (const FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				return Desc->Metadata.Description;
			}

			return FText::GetEmpty();
		}

		const FText& FGraphController::GetOutputDescription(const FString& InName) const
		{
			if (const FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				return Desc->Metadata.Description;
			}

			return FText::GetEmpty();
		}

		void FGraphController::SetInputDisplayName(const FString& InName, const FText& InDisplayName)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				Desc->Metadata.DisplayName = InDisplayName;
			}
		}

		void FGraphController::SetOutputDisplayName(const FString& InName, const FText& InDisplayName)
		{
			if (FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				Desc->Metadata.DisplayName = InDisplayName;
			}
		}

		void FGraphController::SetInputDescription(const FString& InName, const FText& InDescription)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InName))
			{
				Desc->Metadata.Description = InDescription;
			}
		}

		void FGraphController::SetOutputDescription(const FString& InName, const FText& InDescription)
		{
			if (FMetasoundFrontendClassOutput* Desc = FindOutputDescriptionWithName(InName))
			{
				Desc->Metadata.Description = InDescription;
			}
		}

		// This can be used to clear the current literal for a given input.
		// @returns false if the input name couldn't be found.
		bool FGraphController::ClearLiteralForInput(const FString& InInputName, FGuid InVertexID)
		{
			if (FMetasoundFrontendClassInput* Desc = FindInputDescriptionWithName(InInputName))
			{
				Desc->DefaultLiteral.Clear();
			}

			return false;
		}

		FNodeHandle FGraphController::AddNode(const FNodeRegistryKey& InKey)
		{
			// Construct a FNodeClassInfo from this lookup key.
			FConstClassAccessPtr Class = OwningDocument->FindOrAddClass(InKey);
			const bool bIsValidClass = (nullptr != Class.Get());

			if (bIsValidClass)
			{
				return AddNode(Class);
			}

			UE_LOG(LogMetaSound, Warning, TEXT("Failed to find or add node class info with registry key [Key:%s]"), *InKey);
			return INodeController::GetInvalidHandle();
		}

		FNodeHandle FGraphController::AddNode(const FMetasoundFrontendClassMetadata& InClassMetadata)
		{
			return AddNode(NodeRegistryKey::CreateKey(InClassMetadata));
		}

		FNodeHandle FGraphController::AddDuplicateNode(const INodeController& InNode)
		{
			// TODO: will need to copy node interface when dynamic pins exist.
			const FMetasoundFrontendClassMetadata& ClassMetadata = InNode.GetClassMetadata();

			FConstClassAccessPtr ClassPtr;

			if (EMetasoundFrontendClassType::Graph == ClassMetadata.GetType())
			{
				// Add subgraph and dependencies if needed
				ClassPtr = OwningDocument->FindClass(ClassMetadata);
				const bool bIsClassMissing = (nullptr == ClassPtr.Get());

				if (bIsClassMissing)
				{
					// Class does not exist, need to add the subgraph
					OwningDocument->AddDuplicateSubgraph(*(InNode.AsGraph()));
					ClassPtr = OwningDocument->FindClass(ClassMetadata);
				}
			}
			else
			{
				ClassPtr = OwningDocument->FindOrAddClass(ClassMetadata);
			}

			return AddNode(ClassPtr);
		}

		// Remove the node corresponding to this node handle.
		// On success, invalidates the received node handle.
		bool FGraphController::RemoveNode(INodeController& InNode)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				FGuid NodeID = InNode.GetID();
				auto IsNodeWithSameID = [&](const FMetasoundFrontendNode& InDesc) { return InDesc.ID == NodeID; };
				if (const FMetasoundFrontendNode* Desc = GraphClass->Graph.Nodes.FindByPredicate(IsNodeWithSameID))
				{
					switch(InNode.GetClassMetadata().GetType())
					{
						case EMetasoundFrontendClassType::Input:
						{
							return RemoveInput(*Desc);
						}

						case EMetasoundFrontendClassType::Output:
						{
							return RemoveOutput(*Desc);
						}

						case EMetasoundFrontendClassType::Variable:
						case EMetasoundFrontendClassType::External:
						case EMetasoundFrontendClassType::Graph:
						{
							return RemoveNode(*Desc);
						}

						default:
						case EMetasoundFrontendClassType::Invalid:
						{
							static_assert(static_cast<int32>(EMetasoundFrontendClassType::Invalid) == 5, "Possible missing switch case coverage for EMetasoundFrontendClassType.");
							checkNoEntry();
						}
					}
				}
			}

			return false;
		}

		// Returns the metadata for the current graph, including the name, description and author.
		const FMetasoundFrontendClassMetadata& FGraphController::GetGraphMetadata() const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Metadata;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendClassMetadata>();
		}

		void FGraphController::SetGraphMetadata(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				GraphClass->Metadata = InMetadata;
			}
		}

		FNodeHandle FGraphController::CreateEmptySubgraph(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			if (InMetadata.GetType() == EMetasoundFrontendClassType::Graph)
			{
				if (const FMetasoundFrontendClass* ExistingDependency = OwningDocument->FindClass(InMetadata).Get())
				{
					UE_LOG(LogMetaSound, Error, TEXT("Cannot add new subgraph. Metasound class already exists with matching metadata Name: \"%s\", Version %d.%d"), *(ExistingDependency->Metadata.GetClassName().GetFullName().ToString()), ExistingDependency->Metadata.GetVersion().Major, ExistingDependency->Metadata.GetVersion().Minor);
				}
				else 
				{
					return AddNode(OwningDocument->FindOrAddClass(InMetadata));
				}
			}
			else
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Incompatible Metasound NodeType encountered when attempting to create an empty subgraph.  NodeType must equal EMetasoundFrontendClassType::Graph"));
			}
			
			return FInvalidNodeController::GetInvalid();
		}

		TUniquePtr<IOperator> FGraphController::BuildOperator(const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<IOperatorBuilder::FBuildErrorPtr>& OutBuildErrors) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{

				// TODO: Implement subgraph inflation step here.

				// TODO: bubble up errors. 
				const TArray<FMetasoundFrontendGraphClass>& Subgraphs = OwningDocument->GetSubgraphs();
				const TArray<FMetasoundFrontendClass>& Dependencies = OwningDocument->GetDependencies();

				TUniquePtr<FFrontendGraph> Graph = FFrontendGraphBuilder::CreateGraph(*GraphClass, Subgraphs, Dependencies);

				if (!Graph.IsValid())
				{
					return TUniquePtr<IOperator>(nullptr);
				}

				FOperatorBuilder OperatorBuilder(FOperatorBuilderSettings::GetDefaultSettings());
				FBuildGraphParams BuildParams{*Graph, InSettings, FDataReferenceCollection{}, InEnvironment};
				return OperatorBuilder.BuildGraphOperator(BuildParams, OutBuildErrors);
			}
			else
			{
				return TUniquePtr<IOperator>(nullptr);
			}
		}

		FDocumentHandle FGraphController::GetOwningDocument()
		{
			return OwningDocument;
		}

		FConstDocumentHandle FGraphController::GetOwningDocument() const
		{
			return OwningDocument;
		}

		FNodeHandle FGraphController::AddNode(FConstClassAccessPtr InExistingDependency)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				if (const FMetasoundFrontendClass* NodeClass = InExistingDependency.Get())
				{
					FMetasoundFrontendNode& Node = GraphClass->Graph.Nodes.Emplace_GetRef(*NodeClass);

					Node.ID = FGuid::NewGuid();

					FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					return GetNodeHandle(FGraphController::FNodeAndClass{NodePtr, InExistingDependency});
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		bool FGraphController::RemoveNode(const FMetasoundFrontendNode& InDesc)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsEdgeForThisNode = [&](const FMetasoundFrontendEdge& ConDesc) { return (ConDesc.FromNodeID == InDesc.ID) || (ConDesc.ToNodeID == InDesc.ID); };

				// Remove any reference connections
				int32 NumRemoved = GraphClass->Graph.Edges.RemoveAll(IsEdgeForThisNode);

				auto IsNodeWithID = [&](const FMetasoundFrontendNode& Desc) { return InDesc.ID == Desc.ID; };

				NumRemoved += GraphClass->Graph.Nodes.RemoveAll(IsNodeWithID);

				OwningDocument->SynchronizeDependencies();

				return (NumRemoved > 0);
			}
			return false;
		}

		bool FGraphController::RemoveInput(const FMetasoundFrontendNode& InNode)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsInputWithSameNodeID = [&](const FMetasoundFrontendClassInput& ClassInput) { return ClassInput.NodeID == InNode.ID; };

				int32 NumInputsRemoved = GraphClass->Interface.Inputs.RemoveAll(IsInputWithSameNodeID);

				if (NumInputsRemoved > 0)
				{
					GraphClass->Interface.UpdateChangeID();
				}

				bool bDidRemoveNode = RemoveNode(InNode);

				return NumInputsRemoved > 0 || bDidRemoveNode;
			}

			return false;
		}

		bool FGraphController::RemoveOutput(const FMetasoundFrontendNode& InNode)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				auto IsOutputWithSameNodeID = [&](const FMetasoundFrontendClassOutput& ClassOutput) { return ClassOutput.NodeID == InNode.ID; };

				int32 NumOutputsRemoved = GraphClass->Interface.Outputs.RemoveAll(IsOutputWithSameNodeID);

				if (NumOutputsRemoved)
				{
					GraphClass->Interface.UpdateChangeID();
				}

				bool bDidRemoveNode = RemoveNode(InNode);

				return (NumOutputsRemoved > 0) || bDidRemoveNode;
			}

			return false;
		}

		void FGraphController::UpdateInterfaceChangeID()
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				GraphClass->Interface.UpdateChangeID();
			}
		}

		bool FGraphController::ContainsNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					const FMetasoundFrontendClass* NodeClass = OwningDocument->FindClassWithID(Node.ClassID).Get();
					if (nullptr != NodeClass)
					{
						if (InPredicate(*NodeClass, Node))
						{
							return true;
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return false;
		}

		TArray<FGraphController::FNodeAndClass> FGraphController::GetNodesAndClasses()
		{
			TArray<FNodeAndClass> NodesAndClasses;

			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);

					const bool bIsValidNodePtr = (nullptr != NodePtr.Get());
					const bool bIsValidNodeClassPtr = (nullptr != NodeClassPtr.Get());

					if (bIsValidNodePtr && bIsValidNodeClassPtr)
					{
						NodesAndClasses.Add(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FConstNodeAndClass> FGraphController::GetNodesAndClasses() const
		{
			TArray<FConstNodeAndClass> NodesAndClasses;

			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);

					const bool bIsValidNodePtr = (nullptr != NodePtr.Get());
					const bool bIsValidNodeClassPtr = (nullptr != NodeClassPtr.Get());

					if (bIsValidNodePtr && bIsValidNodeClassPtr)
					{
						NodesAndClasses.Add(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FNodeAndClass> FGraphController::GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate)
		{
			TArray<FNodeAndClass> NodesAndClasses;

			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InPredicate(*NodeClass, Node))
						{
							FNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							NodesAndClasses.Add(FGraphController::FNodeAndClass{ NodePtr, NodeClassPtr });
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		TArray<FGraphController::FConstNodeAndClass> FGraphController::GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			TArray<FConstNodeAndClass> NodesAndClasses;

			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				for (const FMetasoundFrontendNode& Node : GraphClass->Graph.Nodes)
				{
					FConstClassAccessPtr NodeClassPtr = OwningDocument->FindClassWithID(Node.ClassID);
					if (const FMetasoundFrontendClass* NodeClass = NodeClassPtr.Get())
					{
						if (InPredicate(*NodeClass, Node))
						{
							FConstNodeAccessPtr NodePtr = GraphClassPtr.GetNodeWithNodeID(Node.ID);
							NodesAndClasses.Add(FGraphController::FConstNodeAndClass{ NodePtr, NodeClassPtr });
						}
					}
					else
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Failed to find class for node [NodeID:%s, ClassID:%s]"), *Node.ID.ToString(), *Node.ClassID.ToString());
					}
				}
			}

			return NodesAndClasses;
		}

		FNodeHandle FGraphController::GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate)
		{
			TArray<FNodeAndClass> NodeAndClass = GetNodesAndClassesByPredicate(InPredicate);
			if (NodeAndClass.Num() > 0)
			{
				return GetNodeHandle(NodeAndClass[0]);
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FGraphController::GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const
		{
			TArray<FConstNodeAndClass> NodeAndClass = GetNodesAndClassesByPredicate(InPredicate);
			if (NodeAndClass.Num() > 0)
			{
				return GetNodeHandle(NodeAndClass[0]);
			}

			return FInvalidNodeController::GetInvalid();
		}

		TArray<FNodeHandle> FGraphController::GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc)
		{
			return GetNodeHandles(GetNodesAndClassesByPredicate(InFilterFunc));
		}

		TArray<FConstNodeHandle> FGraphController::GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc) const
		{
			return GetNodeHandles(GetNodesAndClassesByPredicate(InFilterFunc));
		}

		TArray<FNodeHandle> FGraphController::GetNodeHandles(TArrayView<const FGraphController::FNodeAndClass> InNodesAndClasses)
		{
			TArray<FNodeHandle> Nodes;

			for (const FNodeAndClass& NodeAndClass: InNodesAndClasses)
			{
				FNodeHandle NodeController = GetNodeHandle(NodeAndClass);
				if (NodeController->IsValid())
				{
					Nodes.Add(NodeController);
				}
			}

			return Nodes;
		}

		TArray<FConstNodeHandle> FGraphController::GetNodeHandles(TArrayView<const FGraphController::FConstNodeAndClass> InNodesAndClasses) const
		{
			TArray<FConstNodeHandle> Nodes;

			for (const FConstNodeAndClass& NodeAndClass : InNodesAndClasses)
			{
				FConstNodeHandle NodeController = GetNodeHandle(NodeAndClass);
				if (NodeController->IsValid())
				{
					Nodes.Add(NodeController);
				}
			}

			return Nodes;
		}

		FNodeHandle FGraphController::GetNodeHandle(const FGraphController::FNodeAndClass& InNodeAndClass)
		{
			FMetasoundFrontendNode* Node = InNodeAndClass.Node.Get();
			const FMetasoundFrontendClass* NodeClass = InNodeAndClass.Class.Get();
			
			if ((nullptr != Node) && (nullptr != NodeClass))
			{
				FGraphHandle OwningGraph = this->AsShared();
				FGraphAccessPtr GraphPtr = GraphClassPtr.GetGraph();

				switch (NodeClass->Metadata.GetType())
				{
					case EMetasoundFrontendClassType::Input:
						{
							FClassInputAccessPtr OwningGraphClassInputPtr = FindInputDescriptionWithNodeID(Node->ID);
							if (nullptr != OwningGraphClassInputPtr.Get())
							{
								FInputNodeController::FInitParams InitParams
								{
									InNodeAndClass.Node,
									InNodeAndClass.Class,
									OwningGraphClassInputPtr,
									GraphPtr,
									OwningGraph
								};
								return FInputNodeController::CreateInputNodeHandle(InitParams);
							}
							else
							{
								// TODO: This supports input nodes introduced during subgraph inflation. Input nodes
								// should be replaced with value nodes once they are implemented. 
								FNodeController::FInitParams InitParams
								{
									InNodeAndClass.Node,
									InNodeAndClass.Class,
									GraphPtr,
									OwningGraph
								};
								return FNodeController::CreateNodeHandle(InitParams);
							}
						}
						break;

					case EMetasoundFrontendClassType::Output:
						{
							FClassOutputAccessPtr OwningGraphClassOutputPtr = FindOutputDescriptionWithNodeID(Node->ID);
							if (nullptr != OwningGraphClassOutputPtr.Get())
							{
								FOutputNodeController::FInitParams InitParams
								{
									InNodeAndClass.Node,
									InNodeAndClass.Class,
									OwningGraphClassOutputPtr,
									GraphPtr,
									OwningGraph
								};
								return FOutputNodeController::CreateOutputNodeHandle(InitParams);
							}
							else
							{
								// TODO: This supports output nodes introduced during subgraph inflation. Output nodes
								// should be replaced with value nodes once they are implemented. 
								FNodeController::FInitParams InitParams
								{
									InNodeAndClass.Node,
									InNodeAndClass.Class,
									GraphPtr,
									OwningGraph
								};
								return FNodeController::CreateNodeHandle(InitParams);
							}
						}
						break;

					case EMetasoundFrontendClassType::External:
						{
							FNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FNodeController::CreateNodeHandle(InitParams);
						}
						break;

					case EMetasoundFrontendClassType::Graph:
						{
							FSubgraphNodeController::FInitParams InitParams
							{
								InNodeAndClass.Node,
								InNodeAndClass.Class,
								GraphPtr,
								OwningGraph
							};
							return FSubgraphNodeController::CreateNodeHandle(InitParams);
						}
						break;

					default:
						checkNoEntry();
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FConstNodeHandle FGraphController::GetNodeHandle(const FGraphController::FConstNodeAndClass& InNodeAndClass) const
		{
			const FMetasoundFrontendNode* Node = InNodeAndClass.Node.Get();
			const FMetasoundFrontendClass* NodeClass = InNodeAndClass.Class.Get();
			
			if ((nullptr != Node) && (nullptr != NodeClass))
			{
				FConstGraphHandle OwningGraph = this->AsShared();
				FConstGraphAccessPtr GraphPtr = GraphClassPtr.GetGraph();

				switch (NodeClass->Metadata.GetType())
				{
					case EMetasoundFrontendClassType::Input:
						{
							FConstClassInputAccessPtr OwningGraphClassInputPtr = FindInputDescriptionWithNodeID(Node->ID);
							if (nullptr != OwningGraphClassInputPtr.Get())
							{
								FInputNodeController::FInitParams InitParams
								{
									ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
									InNodeAndClass.Class,
									ConstCastAccessPtr<FClassInputAccessPtr>(OwningGraphClassInputPtr),
									ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
									ConstCastSharedRef<IGraphController>(OwningGraph)
								};
								return FInputNodeController::CreateConstInputNodeHandle(InitParams);
							}
						}
						break;

					case EMetasoundFrontendClassType::Output:
						{
							FConstClassOutputAccessPtr OwningGraphClassOutputPtr = FindOutputDescriptionWithNodeID(Node->ID);
							if (nullptr != OwningGraphClassOutputPtr.Get())
							{
								FOutputNodeController::FInitParams InitParams
								{
									ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
									InNodeAndClass.Class,
									ConstCastAccessPtr<FClassOutputAccessPtr>(OwningGraphClassOutputPtr),
									ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
									ConstCastSharedRef<IGraphController>(OwningGraph)
								};
								return FOutputNodeController::CreateConstOutputNodeHandle(InitParams);
							}
						}
						break;

					case EMetasoundFrontendClassType::External:
					case EMetasoundFrontendClassType::Graph:
						{
							FNodeController::FInitParams InitParams
							{
								ConstCastAccessPtr<FNodeAccessPtr>(InNodeAndClass.Node),
								InNodeAndClass.Class,
								ConstCastAccessPtr<FGraphAccessPtr>(GraphPtr),
								ConstCastSharedRef<IGraphController>(OwningGraph)
							};
							return FNodeController::CreateConstNodeHandle(InitParams);
						}
						break;

					default:
						checkNoEntry();
				}
			}

			return FInvalidNodeController::GetInvalid();
		}

		FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithName(const FString& InName)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				FMetasoundFrontendClassInput* ClassInput = GraphClass->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.Name == InName; });
				if (ClassInput)
				{
					// TODO: This assumes the class input is being mutated due to the adjacent const correct call not being utilized.
					// Make this more explicit rather than risking whether or not the caller is using proper const correctness.
					GraphClass->Interface.UpdateChangeID();
					return ClassInput;
				}
			}
			return nullptr;
		}

		const FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithName(const FString& InName)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				FMetasoundFrontendClassOutput* ClassOutput = GraphClass->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.Name == InName; });
				if (ClassOutput)
				{
					// TODO: This assumes the class input is being mutated due to the adjacent const correct call not being utilized.
					// Make this more explicit rather than risking whether or not the caller is using proper const correctness.
					GraphClass->Interface.UpdateChangeID();
					return ClassOutput;
				}
			}
			return nullptr;
		}

		const FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithName(const FString& InName) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.Name == InName; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithVertexID(const FGuid& InVertexID)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				FMetasoundFrontendClassInput* ClassInput = GraphClass->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.VertexID == InVertexID; });
				if (ClassInput)
				{
					// TODO: This assumes the class input is being mutated due to the adjacent const correct call not being utilized.
					// Make this more explicit rather than risking whether or not the caller is using proper const correctness.
					GraphClass->Interface.UpdateChangeID();
					return ClassInput;
				}
			}
			return nullptr;
		}

		const FMetasoundFrontendClassInput* FGraphController::FindInputDescriptionWithVertexID(const FGuid& InVertexID) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Interface.Inputs.FindByPredicate([&](const FMetasoundFrontendClassInput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithVertexID(const FGuid& InVertexID)
		{
			if (FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				FMetasoundFrontendClassOutput* ClassOutput = GraphClass->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.VertexID == InVertexID; });
				if (ClassOutput)
				{
					// TODO: This assumes the class input is being mutated due to the adjacent const correct call not being utilized.
					// Make this more explicit rather than risking whether or not the caller is using proper const correctness.
					GraphClass->Interface.UpdateChangeID();
					return ClassOutput;
				}
			}
			return nullptr;
		}

		const FMetasoundFrontendClassOutput* FGraphController::FindOutputDescriptionWithVertexID(const FGuid& InVertexID) const
		{
			if (const FMetasoundFrontendGraphClass* GraphClass = GraphClassPtr.Get())
			{
				return GraphClass->Interface.Outputs.FindByPredicate([&](const FMetasoundFrontendClassOutput& Desc) { return Desc.VertexID == InVertexID; });
			}
			return nullptr;
		}

		FClassInputAccessPtr FGraphController::FindInputDescriptionWithNodeID(FGuid InNodeID)
		{
			return GraphClassPtr.GetInputWithNodeID(InNodeID);
		}

		FConstClassInputAccessPtr FGraphController::FindInputDescriptionWithNodeID(FGuid InNodeID) const
		{
			return GraphClassPtr.GetInputWithNodeID(InNodeID);
		}

		FClassOutputAccessPtr FGraphController::FindOutputDescriptionWithNodeID(FGuid InNodeID)
		{
			return GraphClassPtr.GetOutputWithNodeID(InNodeID);
		}

		FConstClassOutputAccessPtr FGraphController::FindOutputDescriptionWithNodeID(FGuid InNodeID) const
		{
			return GraphClassPtr.GetOutputWithNodeID(InNodeID);
		}

		FDocumentAccess FGraphController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.GraphClass = GraphClassPtr;
			Access.ConstGraphClass = GraphClassPtr;

			return Access;
		}

		FConstDocumentAccess FGraphController::ShareAccess() const
		{
			FConstDocumentAccess Access;

			Access.ConstGraphClass = GraphClassPtr;

			return Access;
		}


		//
		// FDocumentController
		//
		FDocumentController::FDocumentController(FDocumentAccessPtr InDocumentPtr)
		:	DocumentPtr(InDocumentPtr)
		{
		}

		bool FDocumentController::IsValid() const
		{
			return (nullptr != DocumentPtr.Get());
		}

		const TArray<FMetasoundFrontendClass>& FDocumentController::GetDependencies() const
		{
			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				return Document->Dependencies;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<TArray<FMetasoundFrontendClass>>();
		}

		const TArray<FMetasoundFrontendGraphClass>& FDocumentController::GetSubgraphs() const
		{
			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				return Document->Subgraphs;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<TArray<FMetasoundFrontendGraphClass>>();
		}

		const FMetasoundFrontendGraphClass& FDocumentController::GetRootGraphClass() const
		{
			if (const FMetasoundFrontendDocument* Doc = DocumentPtr.Get())
			{
				return Doc->RootGraph;
			}
			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendGraphClass>();
		}

		bool FDocumentController::AddDuplicateSubgraph(const FMetasoundFrontendGraphClass& InGraphToCopy, const FMetasoundFrontendDocument& InOtherDocument)
		{
			if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				// Direct copy of subgraph
				bool bSuccess = true;
				FMetasoundFrontendGraphClass SubgraphCopy(InGraphToCopy);

				for (FMetasoundFrontendNode& Node : SubgraphCopy.Graph.Nodes)
				{
					const FGuid OriginalClassID = Node.ClassID;

					auto IsClassWithClassID = [&](const FMetasoundFrontendClass& InClass) -> bool
					{
						return InClass.ID == OriginalClassID;
					};

					if (const FMetasoundFrontendClass* OriginalNodeClass = InOtherDocument.Dependencies.FindByPredicate(IsClassWithClassID))
					{
						// Should not be a graph class since it's in the dependencies list
						check(EMetasoundFrontendClassType::Graph != OriginalNodeClass->Metadata.GetType());

						if (const FMetasoundFrontendClass* NodeClass = FindOrAddClass(OriginalNodeClass->Metadata).Get())
						{
							// All this just to update this ID. Maybe having globally 
							// consistent class IDs would help. Or using the classname & version as
							// a class ID. 
							Node.ClassID = NodeClass->ID;
						}
						else
						{
							UE_LOG(LogMetaSound, Error, TEXT("Failed to add subgraph dependency [Class:%s]"), *OriginalNodeClass->Metadata.GetClassName().ToString());
							bSuccess = false;
						}
					}
					else if (const FMetasoundFrontendGraphClass* OriginalNodeGraphClass = InOtherDocument.Subgraphs.FindByPredicate(IsClassWithClassID))
					{
						bSuccess = bSuccess && AddDuplicateSubgraph(*OriginalNodeGraphClass, InOtherDocument);
						if (!bSuccess)
						{
							break;
						}
					}
					else
					{
						bSuccess = false;
						UE_LOG(LogMetaSound, Error, TEXT("Failed to copy subgraph. Subgraph document is missing dependency info for node [Node:%s, NodeID:%s]"), *Node.Name, *Node.ID.ToString());
					}
				}

				if (bSuccess)
				{
					Document->Subgraphs.Add(SubgraphCopy);
				}

				return bSuccess;
			}

			return false;
		}

		const FMetasoundFrontendVersion& FDocumentController::GetArchetypeVersion() const
		{
			if (const FMetasoundFrontendDocument* Doc = DocumentPtr.Get())
			{
				return Doc->ArchetypeVersion;
			}
			return FMetasoundFrontendVersion::GetInvalid();
		}

		void FDocumentController::SetArchetypeVersion(const FMetasoundFrontendVersion& InVersion)
		{
			if (FMetasoundFrontendDocument* Doc = DocumentPtr.Get())
			{
				Doc->ArchetypeVersion = InVersion;
			}
		}

		FGraphHandle FDocumentController::AddDuplicateSubgraph(const IGraphController& InGraph)
		{
			// TODO: class IDs have issues.. 
			// Currently ClassIDs are just used for internal linking. They need to be fixed up
			// here if swapping documents. In the future, ClassIDs should be unique and consistent
			// across documents and platforms.

			FConstDocumentAccess GraphDocumentAccess = GetSharedAccess(*InGraph.GetOwningDocument());
			const FMetasoundFrontendDocument* OtherDocument = GraphDocumentAccess.ConstDocument.Get();
			if (nullptr == OtherDocument)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add subgraph from invalid document"));
				return IGraphController::GetInvalidHandle();
			}

			FConstDocumentAccess GraphAccess = GetSharedAccess(InGraph);
			const FMetasoundFrontendGraphClass* OtherGraph = GraphAccess.ConstGraphClass.Get();
			if (nullptr == OtherGraph)
			{
				UE_LOG(LogMetaSound, Error, TEXT("Cannot add invalid subgraph to document"));
				return IGraphController::GetInvalidHandle();
			}

			if (AddDuplicateSubgraph(*OtherGraph, *OtherDocument))
			{
				if (const FMetasoundFrontendClass* SubgraphClass = FindClass(OtherGraph->Metadata).Get())
				{
					return GetSubgraphWithClassID(SubgraphClass->ID);
				}
			}

			return IGraphController::GetInvalidHandle();
		}
 

		FConstClassAccessPtr FDocumentController::FindDependencyWithID(FGuid InClassID) const 
		{
			return DocumentPtr.GetDependencyWithID(InClassID);
		}

		FConstGraphClassAccessPtr FDocumentController::FindSubgraphWithID(FGuid InClassID) const
		{
			return DocumentPtr.GetSubgraphWithID(InClassID);
		}

		FConstClassAccessPtr FDocumentController::FindClassWithID(FGuid InClassID) const
		{
			FConstClassAccessPtr MetasoundClass = FindDependencyWithID(InClassID);

			if (nullptr == MetasoundClass.Get())
			{
				MetasoundClass = FindSubgraphWithID(InClassID);
			}

			return MetasoundClass;
		}

		void FDocumentController::SetMetadata(const FMetasoundFrontendDocumentMetadata& InMetadata)
		{
			if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				Document->Metadata = InMetadata;
			}
		}

		const FMetasoundFrontendDocumentMetadata& FDocumentController::GetMetadata() const
		{
			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				return Document->Metadata;
			}

			return FrontendControllerIntrinsics::GetInvalidValueConstRef<FMetasoundFrontendDocumentMetadata>();
		}

		FConstClassAccessPtr FDocumentController::FindClass(const FNodeRegistryKey& InKey) const
		{
			return DocumentPtr.GetClassWithRegistryKey(InKey);
		}

		FConstClassAccessPtr FDocumentController::FindOrAddClass(const FNodeRegistryKey& InKey)
		{
			if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				FConstClassAccessPtr ClassPtr = FindClass(InKey);

				auto AddClass = [=](FMetasoundFrontendClass&& NewClassDescription)
				{
					FConstClassAccessPtr NewClassPtr;

					// Cannot add a subgraph using this method because dependencies
					// of external graph are not added in this method.
					check(EMetasoundFrontendClassType::Graph != NewClassDescription.Metadata.GetType());
					NewClassDescription.ID = FGuid::NewGuid();

					Document->Dependencies.Add(MoveTemp(NewClassDescription));
					NewClassPtr = FindClass(InKey);

					return NewClassPtr;
				};

				if (const FMetasoundFrontendClass* MetasoundClass = ClassPtr.Get())
				{
					// External node classes must match version to return shared definition.
					if (MetasoundClass->Metadata.GetType() == EMetasoundFrontendClassType::External)
					{
						// TODO: Assuming we want to recheck classes when they add another
						// node, this should be replace with a call to synchronize a 
						// single class.
						FMetasoundFrontendClass NewClass = GenerateClassDescription(InKey);
						if (NewClass.Metadata.GetVersion().Major != MetasoundClass->Metadata.GetVersion().Major)
						{
							return AddClass(MoveTemp(NewClass));
						}
					}

					return ClassPtr;
				}

				FMetasoundFrontendClass NewClass = GenerateClassDescription(InKey);
				return AddClass(MoveTemp(NewClass));
			}

			return FConstClassAccessPtr();
		}

		FConstClassAccessPtr FDocumentController::FindClass(const FMetasoundFrontendClassMetadata& InMetadata) const
		{
			return DocumentPtr.GetClassWithMetadata(InMetadata);
		}

		FConstClassAccessPtr FDocumentController::FindOrAddClass(const FMetasoundFrontendClassMetadata& InMetadata)
		{
			FConstClassAccessPtr ClassPtr = FindClass(InMetadata);
			
			if (const FMetasoundFrontendClass* Class = ClassPtr.Get())
			{
				// External node classes must match major version to return shared definition.
				if (EMetasoundFrontendClassType::External == InMetadata.GetType())
				{
					if (InMetadata.GetVersion().Major != Class->Metadata.GetVersion().Major)
					{
						// Mismatched major version. Reset class pointer to null.
						ClassPtr = FConstClassAccessPtr();
					}
				}
			}


			const bool bNoMatchingClassFoundInDocument = (nullptr == ClassPtr.Get());
			if (bNoMatchingClassFoundInDocument)
			{
				// If no matching class found, attempt to add a class matching the metadata.
				if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
				{
					switch (InMetadata.GetType())
					{
						case EMetasoundFrontendClassType::External:
						case EMetasoundFrontendClassType::Input:
						case EMetasoundFrontendClassType::Output:
						{
							FMetasoundFrontendClass NewClass;
							FNodeRegistryKey Key = NodeRegistryKey::CreateKey(InMetadata);

							if (FRegistry::GetFrontendClassFromRegistered(Key, NewClass))
							{
								NewClass.ID = FGuid::NewGuid();
								Document->Dependencies.Add(NewClass);
							}
							else
							{
								UE_LOG(LogMetaSound, Error,
									TEXT("Cannot add external dependency. No Metasound class found with matching registry key [Key:%s, Name:%s, Version:%s]. Suggested solution \"%s\" by %s."),
									*Key,
									*InMetadata.GetClassName().GetFullName().ToString(),
									*InMetadata.GetVersion().ToString(),
									*InMetadata.GetPromptIfMissing().ToString(),
									*InMetadata.GetAuthor().ToString());
							}
						}
						break;

						case EMetasoundFrontendClassType::Graph:
						{
							FMetasoundFrontendGraphClass NewClass;
							NewClass.ID = FGuid::NewGuid();
							NewClass.Metadata = InMetadata;

							Document->Subgraphs.Add(NewClass);
						}
						break;

						default:
						{
							UE_LOG(LogMetaSound, Error, TEXT(
								"Unsupported metasound class type for node: \"%s\" (%s)."),
								*InMetadata.GetClassName().GetFullName().ToString(),
								*InMetadata.GetVersion().ToString());
							checkNoEntry();
						}
					}

					ClassPtr = FindClass(InMetadata);
				}
			}

			return ClassPtr;
		}

		const FMetasoundFrontendClass* FDocumentController::SynchronizeDependency(const FNodeRegistryKey& InKey)
		{
			FMetasoundFrontendDocument* Document = DocumentPtr.Get();
			if (!ensure(Document))
			{
				return nullptr;
			}

			for (FMetasoundFrontendClass& Class : Document->Dependencies)
			{
				const FNodeRegistryKey DependencyRegistryKey = FMetasoundFrontendRegistryContainer::Get()->GetRegistryKey(Class.Metadata);
				if (InKey == DependencyRegistryKey)
				{
					const FGuid ClassID = Class.ID;
					FMetasoundFrontendClass RegisteredClass = GenerateClassDescription(InKey);
					RegisteredClass.ID = ClassID;
					Class = RegisteredClass;
					return &Class;
				}
			}

			FMetasoundFrontendClass NewClass = GenerateClassDescription(InKey);
			NewClass.ID = FGuid::NewGuid();
			return &Document->Dependencies.Add_GetRef(NewClass);
		}

		void FDocumentController::SynchronizeDependencies()
		{
			if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				int32 NumDependenciesRemovedThisItr = 0;

				// Repeatedly remove unreferenced dependencies until there are
				// no unreferenced dependencies left.
				do
				{
					TSet<FGuid> ReferencedDependencyIDs;
					auto AddNodeClassIDToSet = [&](const FMetasoundFrontendNode& Node)
					{
						ReferencedDependencyIDs.Add(Node.ClassID);
					};

					auto AddGraphNodeClassIDsToSet = [&](const FMetasoundFrontendGraphClass& GraphClass)
					{
						Algo::ForEach(GraphClass.Graph.Nodes, AddNodeClassIDToSet);
					};

					// Referenced dependencies in root class
					Algo::ForEach(Document->RootGraph.Graph.Nodes, AddNodeClassIDToSet);

					// Referenced dependencies in subgraphs
					Algo::ForEach(Document->Subgraphs, AddGraphNodeClassIDsToSet);

					auto IsDependencyUnreferenced = [&](const FMetasoundFrontendClass& ClassDependency)
					{
						return !ReferencedDependencyIDs.Contains(ClassDependency.ID);
					};

					NumDependenciesRemovedThisItr = Document->Dependencies.RemoveAllSwap(IsDependencyUnreferenced);
				}
				while (NumDependenciesRemovedThisItr > 0);
			}
		}

		FGraphHandle FDocumentController::GetRootGraph()
		{
			if (IsValid())
			{
				FGraphClassAccessPtr GraphClass = DocumentPtr.GetRootGraph();
				return FGraphController::CreateGraphHandle(FGraphController::FInitParams{GraphClass, this->AsShared()});
			}

			return FInvalidGraphController::GetInvalid();
		}

		FConstGraphHandle FDocumentController::GetRootGraph() const
		{
			if (IsValid())
			{
				FConstGraphClassAccessPtr GraphClass = DocumentPtr.GetRootGraph();
				return FGraphController::CreateConstGraphHandle(FGraphController::FInitParams
					{
						ConstCastAccessPtr<FGraphClassAccessPtr>(GraphClass),
						ConstCastSharedRef<IDocumentController>(this->AsShared())
					});
			}
			return FInvalidGraphController::GetInvalid();
		}

		TArray<FGraphHandle> FDocumentController::GetSubgraphHandles() 
		{
			TArray<FGraphHandle> Subgraphs;

			if (FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				for (FMetasoundFrontendGraphClass& GraphClass : Document->Subgraphs)
				{
					Subgraphs.Add(GetSubgraphWithClassID(GraphClass.ID));
				}
			}

			return Subgraphs;
		}

		TArray<FConstGraphHandle> FDocumentController::GetSubgraphHandles() const 
		{
			TArray<FConstGraphHandle> Subgraphs;

			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				for (const FMetasoundFrontendGraphClass& GraphClass : Document->Subgraphs)
				{
					Subgraphs.Add(GetSubgraphWithClassID(GraphClass.ID));
				}
			}

			return Subgraphs;
		}

		FGraphHandle FDocumentController::GetSubgraphWithClassID(FGuid InClassID)
		{
			FGraphClassAccessPtr GraphClassPtr = DocumentPtr.GetSubgraphWithID(InClassID);

			return FGraphController::CreateGraphHandle(FGraphController::FInitParams{GraphClassPtr, this->AsShared()});
		}

		FConstGraphHandle FDocumentController::GetSubgraphWithClassID(FGuid InClassID) const
		{
			FConstGraphClassAccessPtr GraphClassPtr = DocumentPtr.GetSubgraphWithID(InClassID);

			return FGraphController::CreateConstGraphHandle(FGraphController::FInitParams{ConstCastAccessPtr<FGraphClassAccessPtr>(GraphClassPtr), ConstCastSharedRef<IDocumentController>(this->AsShared())});
		}

		bool FDocumentController::ExportToJSONAsset(const FString& InAbsolutePath) const
		{
			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
				{
					TJsonStructSerializerBackend<DefaultCharType> Backend(*FileWriter, EStructSerializerBackendFlags::Default);
					FStructSerializer::Serialize<FMetasoundFrontendDocument>(*Document, Backend);
			
					FileWriter->Close();

					return true;
				}
				else
				{
					UE_LOG(LogMetaSound, Error, TEXT("Failed to export Metasound json asset. Could not write to path \"%s\"."), *InAbsolutePath);
				}
			}

			return false;
		}

		FString FDocumentController::ExportToJSON() const
		{
			FString Output;

			if (const FMetasoundFrontendDocument* Document = DocumentPtr.Get())
			{
				TArray<uint8> WriterBuffer;
				FMemoryWriter MemWriter(WriterBuffer);

				Metasound::TJsonStructSerializerBackend<Metasound::DefaultCharType> Backend(MemWriter, EStructSerializerBackendFlags::Default);
				FStructSerializer::Serialize<FMetasoundFrontendDocument>(*Document, Backend);

				MemWriter.Close();

				// null terminator
				WriterBuffer.AddZeroed(sizeof(ANSICHAR));

				Output.AppendChars(reinterpret_cast<ANSICHAR*>(WriterBuffer.GetData()), WriterBuffer.Num() / sizeof(ANSICHAR));
			}

			return Output;
		}
		
		FDocumentAccess FDocumentController::ShareAccess() 
		{
			FDocumentAccess Access;

			Access.Document = DocumentPtr;
			Access.ConstDocument = DocumentPtr;

			return Access;
		}

		FConstDocumentAccess FDocumentController::ShareAccess() const 
		{
			FConstDocumentAccess Access;

			Access.ConstDocument = DocumentPtr;

			return Access;
		}
	}
}
#undef LOCTEXT_NAMESPACE
