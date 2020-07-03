// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontend.h"

#include "Backends/JsonStructSerializerBackend.h"
#include "StructSerializer.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

#include "MetasoundNodeAsset.h"
#include "MetasoundAudioFormats.h"


static int32 MetasoundUndoRollLimitCvar = 128;
FAutoConsoleVariableRef CVarMetasoundUndoRollLimit(
	TEXT("au.Metasound.Frontend.UndoRollLimit"),
	MetasoundUndoRollLimitCvar,
	TEXT("Sets the maximum size of our undo buffer for graph editing in the Metasound Frontend.\n")
	TEXT("n: Number of undoable actions we buffer."),
	ECVF_Default);

namespace Metasound
{
	namespace Frontend
	{
		TArray<FNodeClassInfo> GetAllAvailableNodeClasses()
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return TArray<FNodeClassInfo>();
		}

		TArray<FNodeClassInfo> GetAllNodeClassesInNamespace(const FString& InNamespace)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return TArray<FNodeClassInfo>();
		}

		TArray<FNodeClassInfo> GetAllNodesWhoseNameContains(const FString& InSubstring)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return TArray<FNodeClassInfo>();
		}

		TArray<FNodeClassInfo> GetAllNodesWithAnOutputOfType(const FName& InType)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return TArray<FNodeClassInfo>();
		}

		TArray<FNodeClassInfo> GetAllNodesWithAnInputOfType(const FName& InType)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return TArray<FNodeClassInfo>();
		}

		// gets all metadata (name, description, author, what to say if it's missing) for a given node.
		FMetasoundClassMetadata GetMetadataForNode(const FNodeClassInfo& InInfo)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return FMetasoundClassMetadata();
		}

		FMetasoundClassDescription GetClassDescriptionForNode(const FNodeClassInfo& InInfo)
		{
			// UEAU-471
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return FMetasoundClassDescription();
		}

		FInputHandle::FInputHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams, const FString& InputName)
			: ITransactable(MetasoundUndoRollLimitCvar, InParams.InOwningAsset)
			, NodePtr(InParams.InAccessPoint, InParams.InPath)
			, NodeClass(InParams.InAccessPoint, Path::GetDependencyPath(InParams.InClassName))
			, InputPtr(InParams.InAccessPoint, NodeClass.GetPath()[Path::EFromClass::ToInputs][*InputName])
		{
			if (InParams.InAccessPoint.IsValid())
			{
				// Test both pointers to the graph and it's owning class description.
				ensureAlwaysMsgf(NodePtr.IsValid() && NodeClass.IsValid() && InputPtr.IsValid(), TEXT("Tried to build FGraphHandle with Invalid Path: %s"), *Path::GetPrintableString(InParams.InPath));
			}
		}

		FInputHandle FInputHandle::InvalidHandle()
		{
			FDescPath NullPath = FDescPath();
			FString NullString = FString();
			FHandleInitParams InitParams = { nullptr, NullPath , NullString, nullptr };
			return FInputHandle(FHandleInitParams::PrivateToken, InitParams, NullString);
		}

		bool FInputHandle::IsValid() const
		{
			return NodePtr.IsValid() && NodeClass.IsValid() && InputPtr.IsValid();
		}

		bool FInputHandle::IsConnected() const
		{
			if (!IsValid())
			{
				return false;
			}

			const FMetasoundNodeConnectionDescription* Connection = GetConnectionDescription();
			if (Connection)
			{
				return Connection->NodeID != FMetasoundNodeConnectionDescription::DisconnectedNodeID;
			}
			else
			{
				return false;
			}
		}

		FName FInputHandle::GetInputType() const
		{
			if (!IsValid())
			{
				return FName();
			}

			return InputPtr->TypeName;
		}

		FString FInputHandle::GetInputName() const
		{
			if (!IsValid())
			{
				return FString();
			}

			return InputPtr->Name;
		}

		FString FInputHandle::GetInputTooltip() const
		{
			if (!IsValid())
			{
				return FString();
			}

			return InputPtr->ToolTip;
		}

		FOutputHandle FInputHandle::GetCurrentlyConnectedOutput() const
		{
			if (!IsValid())
			{
				return FOutputHandle::InvalidHandle();
			}

			if (!IsConnected())
			{
				return FOutputHandle::InvalidHandle();
			}

			const FMetasoundNodeConnectionDescription* Connection = GetConnectionDescription();
			check(Connection);

			FString OutputName = Connection->OutputNameOrLiteral;
			uint32 OutputNodeID = Connection->NodeID;

			// All node connections are in the same graph, so we just need to go up one level to the Nodes array
			// and look up the node by it's unique ID.
			FDescPath OutputNodePath = (NodePtr.GetPath() << 1)[OutputNodeID];
			FHandleInitParams InitParams = { NodePtr.GetAccessPoint(), OutputNodePath, NodeClass->Metadata.NodeName, OwningAsset };

			return FOutputHandle(FHandleInitParams::PrivateToken, InitParams,  OutputName);
		}

		FConnectability FInputHandle::CanConnectTo(const FOutputHandle& InHandle) const
		{
			FConnectability OutConnectability;
			OutConnectability.Connectable = FConnectability::EConnectable::No;

			if (InHandle.GetOutputType() == GetInputType())
			{
				OutConnectability.Connectable = FConnectability::EConnectable::Yes;
				return OutConnectability;
			}

			//@todo: scan for possible converter nodes here. (UEAU-473)
			return OutConnectability;
		}

		bool FInputHandle::Connect(FOutputHandle& InHandle)
		{
			if (!IsValid() || !InHandle.IsValid())
			{
				return false;
			}

			if (!ensureAlwaysMsgf(InHandle.GetOutputType() == GetInputType(), TEXT("Tried to connect incompatible types!")))
			{
				return false;
			}

			uint32 OutputNodeID = InHandle.GetOwningNodeID();
			FString OutputName = InHandle.GetOutputName();

			FMetasoundNodeConnectionDescription* Connection = GetConnectionDescription();

			if (!Connection)
			{
				TArray<FMetasoundNodeConnectionDescription>& Connections = NodePtr->InputConnections;
				FMetasoundNodeConnectionDescription NewConnection;
				NewConnection.InputName = GetInputName();
				Connection = &Connections.Add_GetRef(NewConnection);
			}

			Connection->NodeID = OutputNodeID;
			Connection->OutputNameOrLiteral = OutputName;

			return true;
		}

		bool FInputHandle::ConnectWithConverterNode(FOutputHandle& InHandle, FString& InNodeClassName)
		{
			// (UEAU-473)
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			return false;
		}

		const FMetasoundNodeConnectionDescription* FInputHandle::GetConnectionDescription() const
		{
			if (!IsValid())
			{
				return nullptr;
			}

			const TArray<FMetasoundNodeConnectionDescription>& Connections = (*NodePtr).InputConnections;

			for (const FMetasoundNodeConnectionDescription& Connection : Connections)
			{
				if (Connection.InputName == GetInputName())
				{
					return &Connection;
				}
			}

			return nullptr;
		}

		FMetasoundNodeConnectionDescription* FInputHandle::GetConnectionDescription()
		{
			if (!IsValid())
			{
				return nullptr;
			}

			TArray<FMetasoundNodeConnectionDescription>& Connections = (*NodePtr).InputConnections;

			for (FMetasoundNodeConnectionDescription& Connection : Connections)
			{
				if (Connection.InputName == GetInputName())
				{
					return &Connection;
				}
			}

			return nullptr;
		}

		FOutputHandle::FOutputHandle(FHandleInitParams::EPrivateToken InToken /* unused */, const FHandleInitParams& InParams, const FString& InOutputName)
			: ITransactable(MetasoundUndoRollLimitCvar, InParams.InOwningAsset)
			, NodePtr(InParams.InAccessPoint, InParams.InPath)
			, NodeClass(InParams.InAccessPoint, Path::GetDependencyPath(InParams.InClassName))
			, OutputPtr(InParams.InAccessPoint, NodeClass.GetPath()[Path::EFromClass::ToOutputs][*InOutputName])
		{
			if (InParams.InAccessPoint.IsValid())
			{
				// Test both pointers to the graph and it's owning class description.
				ensureAlwaysMsgf(NodePtr.IsValid() && NodeClass.IsValid() && OutputPtr.IsValid(), TEXT("Tried to build FGraphHandle with Invalid Path: %s"), *Path::GetPrintableString(InParams.InPath));
			}
		}

		FOutputHandle FOutputHandle::InvalidHandle()
		{
			FString NullString;
			FDescPath NullPath;
			FHandleInitParams InitParams = { nullptr, NullPath, NullString, nullptr };
			return FOutputHandle(FHandleInitParams::PrivateToken, InitParams, NullString);
		}

		bool FOutputHandle::IsValid() const
		{
			return NodePtr.IsValid() && NodeClass.IsValid() && OutputPtr.IsValid();
		}

		FName FOutputHandle::GetOutputType() const
		{
			if (!IsValid())
			{
				return FName();
			}

			return OutputPtr->TypeName;
		}

		FString FOutputHandle::GetOutputName() const
		{
			if (!IsValid())
			{
				return FString();
			}

			return OutputPtr->Name;
		}

		FString FOutputHandle::GetOutputTooltip() const
		{
			if (!IsValid())
			{
				return FString();
			}

			return OutputPtr->ToolTip;
		}

		uint32 FOutputHandle::GetOwningNodeID() const
		{
			if (!IsValid())
			{
				return INDEX_NONE;
			}

			return NodePtr->UniqueID;
		}

		FConnectability FOutputHandle::CanConnectTo(const FInputHandle& InHandle) const
		{
			return InHandle.CanConnectTo(*this);
		}

		bool FOutputHandle::Connect(FInputHandle& InHandle)
		{
			if (!IsValid() || !InHandle.IsValid())
			{
				return false;
			}

			return InHandle.Connect(*this);
		}

		bool FOutputHandle::ConnectWithConverterNode(FInputHandle& InHandle, FString& InNodeClassName)
		{
			return InHandle.ConnectWithConverterNode(*this, InNodeClassName);
		}

		FNodeHandle::FNodeHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams)
			: ITransactable(MetasoundUndoRollLimitCvar, InParams.InOwningAsset)
			, NodePtr(InParams.InAccessPoint, InParams.InPath)
			, NodeClass(InParams.InAccessPoint, Path::GetDependencyPath(InParams.InClassName))
		{
			if (InParams.InAccessPoint.IsValid())
			{
				// Test both pointers to the graph and it's owning class description.
				ensureAlwaysMsgf(NodePtr.IsValid() && NodeClass.IsValid(), TEXT("Tried to build FGraphHandle with Invalid Path: %s"), *Path::GetPrintableString(InParams.InPath));
			}
		}

		FNodeHandle FNodeHandle::InvalidHandle()
		{
			FDescPath InvalidPath;
			FString InvalidClassName;
			FHandleInitParams InitParams = { nullptr, InvalidPath, InvalidClassName, nullptr };
			return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
		}

		bool FNodeHandle::IsValid() const
		{
			return NodePtr.IsValid() && NodeClass.IsValid();
		}

		TArray<FInputHandle> FNodeHandle::GetAllInputs()
		{
			TArray<FInputHandle> OutArray;

			if (!IsValid())
			{
				return OutArray;
			}

			TArray<FMetasoundInputDescription>& InputDescriptions = NodeClass->Inputs;

			for (FMetasoundInputDescription& InputDescription : InputDescriptions)
			{
				FDescPath NodePath = NodePtr.GetPath();
				FString ClassName = GetNodeClassName();
				FHandleInitParams InitParams = { NodePtr.GetAccessPoint(), NodePath, ClassName , OwningAsset };
				OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams, InputDescription.Name);
			}

			return OutArray;
		}

		TArray<FOutputHandle> FNodeHandle::GetAllOutputs()
		{
			TArray<FOutputHandle> OutArray;

			if (!IsValid())
			{
				return OutArray;
			}

			TArray<FMetasoundOutputDescription>& OutputDescriptions = NodeClass->Outputs;

			for (FMetasoundOutputDescription& OutputDescription : OutputDescriptions)
			{
				FDescPath NodePath = NodePtr.GetPath();
				FString NodeClassName = GetNodeClassName();
				FHandleInitParams InitParams = { NodePtr.GetAccessPoint(), NodePath, NodeClassName, OwningAsset };
				OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams, OutputDescription.Name);
			}

			return OutArray;
		}

		FInputHandle FNodeHandle::GetInputWithName(const FString& InName)
		{
			if (!IsValid())
			{
				return FInputHandle::InvalidHandle();
			}

			TArray<FMetasoundInputDescription>& InputDescriptions = NodeClass->Inputs;

			for (FMetasoundInputDescription& InputDescription : InputDescriptions)
			{
				if (InputDescription.Name == InName)
				{
					FString ClassName = GetNodeClassName();
					FHandleInitParams InitParams = { NodePtr.GetAccessPoint(), NodePtr.GetPath(), ClassName, OwningAsset };
					return FInputHandle(FHandleInitParams::PrivateToken, InitParams, InputDescription.Name);
				}
			}

			ensureAlwaysMsgf(false, TEXT("Couldn't find an input with this name on this node!"));
			return FInputHandle::InvalidHandle();
		}

		FOutputHandle FNodeHandle::GetOutputWithName(const FString& InName)
		{
			if (!IsValid())
			{
				return FOutputHandle::InvalidHandle();
			}

			TArray<FMetasoundOutputDescription>& OutputDescriptions = NodeClass->Outputs;

			for (FMetasoundOutputDescription& OutputDescription : OutputDescriptions)
			{
				if (OutputDescription.Name == InName)
				{
					FString NodeClassName = GetNodeClassName();
					FHandleInitParams InitParams = { NodePtr.GetAccessPoint(), NodePtr.GetPath(), NodeClassName , OwningAsset };
					return FOutputHandle(FHandleInitParams::PrivateToken, InitParams, OutputDescription.Name);
				}
			}

			ensureAlwaysMsgf(false, TEXT("Couldn't find an output with this name on this node!"));
			return FOutputHandle::InvalidHandle();
		}

		EMetasoundClassType FNodeHandle::GetNodeType()
		{
			if (!IsValid())
			{
				return EMetasoundClassType::Invalid;
			}

			return NodeClass->Metadata.NodeType;
		}

		FString FNodeHandle::GetNodeClassName()
		{
			if (!IsValid())
			{
				return FString();
			}

			return NodeClass->Metadata.NodeName;
		}

		void FNodeHandle::GetContainedGraph(FGraphHandle& OutGraph)
		{
			if (!IsValid())
			{
				OutGraph = FGraphHandle::InvalidHandle();
			}

			if (!ensureAlwaysMsgf(GetNodeType() == EMetasoundClassType::MetasoundGraph, TEXT("Tried to get the Metasound Graph for a node that was not a Metasound graph.")))
			{
				OutGraph = FGraphHandle::InvalidHandle();
			}

			FDescPath ContainedGraphPath = NodeClass.GetPath()[Path::EFromClass::ToGraph];
			FHandleInitParams InitParams = { NodeClass.GetAccessPoint(), ContainedGraphPath, NodeClass->Metadata.NodeName, OwningAsset };
			// Todo: link this up to look for externally implemented graphs as well.
			OutGraph = FGraphHandle(FHandleInitParams::PrivateToken, InitParams);
		}

		uint32 FNodeHandle::GetNodeID()
		{
			if (!IsValid())
			{
				return INDEX_NONE;
			}

			return NodePtr->UniqueID;
		}

		uint32 FNodeHandle::GetNodeID(const FDescPath& InNodePath)
		{
			if (!ensureAlwaysMsgf(InNodePath.Path.Num() != 0, TEXT("Tried to get a node ID from an empty path.")))
			{
				return INDEX_NONE;
			}

			const Path::FElement& LastElementInPath = InNodePath.Path.Last();
			
			if (!ensureAlwaysMsgf(LastElementInPath.CurrentDescType == Path::EDescType::Node, TEXT("Tried to get the node ID for a path that was not set up for a node.")))
			{
				return INDEX_NONE;
			}

			return LastElementInPath.LookupID;
		}

		FGraphHandle::FGraphHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams)
			: ITransactable(MetasoundUndoRollLimitCvar, InParams.InOwningAsset)
			, GraphPtr(InParams.InAccessPoint, InParams.InPath)
			, GraphsClassDeclaration(InParams.InAccessPoint, Path::GetOwningClassDescription(InParams.InPath))
			, OwningDocument(InParams.InAccessPoint, FDescPath())
		{
			if (InParams.InAccessPoint.IsValid())
			{
				// Test both pointers to the graph and it's owning class description.
				ensureAlwaysMsgf(GraphPtr.IsValid() && GraphsClassDeclaration.IsValid(), TEXT("Tried to build FGraphHandle with Invalid Path: %s"), *Path::GetPrintableString(InParams.InPath));
			}
		}

		uint32 FGraphHandle::FindNewUniqueNodeId()
		{
			// Assumption here is that we will never need more than ten thousand nodes,
			// and four digits are easy enough to read/remember when looking at metasound graph documents.
			static const uint32 NodeIDMax = 9999;

			if (!IsValid())
			{
				return INDEX_NONE;
			}

			TArray<FMetasoundNodeDescription>& Nodes = GraphPtr->Nodes;

			if (!ensureAlwaysMsgf(((uint32)Nodes.Num()) < NodeIDMax, TEXT("Too many nodes to guarantee a unique node ID. Increase the value of NodeIDMax.")))
			{
				return INDEX_NONE;
			}

			while (uint32 RandomID = FMath::RandRange(1, NodeIDMax))
			{
				// Scan through the nodes in this graph to see if they match this ID.
				// If it does, generate a new random ID.
				bool bFoundMatch = false;
				for (FMetasoundNodeDescription& Node : Nodes)
				{
					if (Node.UniqueID == RandomID)
					{
						bFoundMatch = true;
						break;
					}
				}

				if (!bFoundMatch)
				{
					return RandomID;
				}
			}

			return INDEX_NONE;
		}

		uint32 FGraphHandle::FindNewUniqueDependencyId()
		{
			// Assumption here is that we will never need more than ten thousand dependencies,
			// and four digits are easy enough to read/remember when looking at metasound graph documents.
			static const uint32 DependencyIDMax = 9999;

			if (!IsValid())
			{
				return INDEX_NONE;
			}

			TArray<FMetasoundClassDescription>& Dependencies = OwningDocument->Dependencies;

			if (!ensureAlwaysMsgf(Dependencies.Num() < DependencyIDMax, TEXT("Too many nodes to guarantee a unique node ID. Increase the value of NodeIDMax.")))
			{
				return INDEX_NONE;
			}

			while (uint32 RandomID = FMath::RandRange(1, DependencyIDMax))
			{
				// Scan through the nodes in this graph to see if they match this ID.
				// If it does, generate a new random ID.
				bool bFoundMatch = false;
				for (FMetasoundClassDescription& Dependency : Dependencies)
				{
					if (Dependency.UniqueID == RandomID)
					{
						bFoundMatch = true;
						break;
					}
				}

				if (!bFoundMatch)
				{
					return RandomID;
				}
			}

			return INDEX_NONE;
		}

		FGraphHandle FGraphHandle::InvalidHandle()
		{
			FDescPath InvalidPath;
			FString InvalidClassName;
			FHandleInitParams InitParams = { nullptr, InvalidPath, InvalidClassName, nullptr };
			return FGraphHandle(FHandleInitParams::Token, InitParams);
		}

		bool FGraphHandle::IsValid()
		{
			return GraphPtr.IsValid() && GraphsClassDeclaration.IsValid();
		}

		TArray<FNodeHandle> FGraphHandle::GetAllNodes()
		{
			TArray<FNodeHandle> OutArray;

			if (!IsValid())
			{
				return OutArray;
			}
			
			const TArray<FMetasoundNodeDescription>& NodeDescriptions = GraphPtr->Nodes;
			for (const FMetasoundNodeDescription& NodeDescription : NodeDescriptions)
			{
				FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NodeDescription.UniqueID];

				FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NodeDescription.Name, OwningAsset };
				OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams);
			}

			return OutArray;
		}

		TArray<FNodeHandle> FGraphHandle::GetOutputNodes()
		{
			TArray<FNodeHandle> OutArray;

			if (!IsValid())
			{
				return OutArray;
			}

			const TArray<FMetasoundNodeDescription>& NodeDescriptions = GraphPtr->Nodes;
			for (const FMetasoundNodeDescription& NodeDescription : NodeDescriptions)
			{
				if (NodeDescription.ObjectTypeOfNode == EMetasoundClassType::Output)
				{
					FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NodeDescription.UniqueID];
					FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NodeDescription.Name, OwningAsset };
					OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams);
				}
			}

			return OutArray;
		}

		TArray<FNodeHandle> FGraphHandle::GetInputNodes()
		{
			TArray<FNodeHandle> OutArray;

			if (!IsValid())
			{
				return OutArray;
			}

			const TArray<FMetasoundNodeDescription>& NodeDescriptions = GraphPtr->Nodes;
			for (const FMetasoundNodeDescription& NodeDescription : NodeDescriptions)
			{
				if (NodeDescription.ObjectTypeOfNode == EMetasoundClassType::Input)
				{
					FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NodeDescription.UniqueID];
					FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NodeDescription.Name, OwningAsset };
					OutArray.Emplace(FHandleInitParams::PrivateToken, InitParams);
				}
			}

			return OutArray;
		}

		FNodeHandle FGraphHandle::GetOutputNodeWithName(const FString& InName)
		{
			if (!IsValid())
			{
				return FNodeHandle::InvalidHandle();
			}

			const TArray<FMetasoundNodeDescription>& NodeDescriptions = GraphPtr->Nodes;
			for (const FMetasoundNodeDescription& NodeDescription : NodeDescriptions)
			{
				if (NodeDescription.ObjectTypeOfNode == EMetasoundClassType::Output && NodeDescription.Name == InName)
				{
					FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NodeDescription.UniqueID];

					FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, InName, OwningAsset };
					return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
				}
			}

			ensureAlwaysMsgf(false, TEXT("Tried to get output node %s, but it didn't exist"), *InName);
			return FNodeHandle::InvalidHandle();
		}

		FNodeHandle FGraphHandle::GetInputNodeWithName(const FString& InName)
		{
			if (!IsValid())
			{
				return FNodeHandle::InvalidHandle();
			}

			const TArray<FMetasoundNodeDescription>& NodeDescriptions = GraphPtr->Nodes;
			for (const FMetasoundNodeDescription& NodeDescription : NodeDescriptions)
			{
				if (NodeDescription.ObjectTypeOfNode == EMetasoundClassType::Input && NodeDescription.Name == InName)
				{
					FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NodeDescription.UniqueID];

					// todo: add special enum for input/output nodes
					FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, InName, OwningAsset };
					return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
				}
			}

			ensureAlwaysMsgf(false, TEXT("Tried to get output node %s, but it didn't exist"), *InName);
			return FNodeHandle::InvalidHandle();
		}

		FNodeHandle FGraphHandle::AddNewInput(const FMetasoundInputDescription& InDescription)
		{
			if (!IsValid())
			{
				return FNodeHandle::InvalidHandle();
			}
			
			// @todo: verify that InDescription.TypeName is a valid Metasound type.

			const uint32 NewUniqueId = FindNewUniqueNodeId();
			
			if (!ensureAlwaysMsgf(NewUniqueId != INDEX_NONE, TEXT("FindNewUniqueNodeId failed!")))
			{
				return FNodeHandle::InvalidHandle();
			}

			
			TArray<FMetasoundInputDescription>& Inputs = GraphsClassDeclaration->Inputs;

			// Sanity check that this input has a unique name.
			for (const FMetasoundInputDescription& Input : Inputs)
			{
				if (!ensureAlwaysMsgf(Input.Name != InDescription.Name, TEXT("Tried to add a new input with a name that already exists!")))
				{
					return FNodeHandle::InvalidHandle();
				}
			}

			// Add the input to this node's class description.
			Inputs.Add(InDescription);

			FMetasoundNodeDescription NewNodeDescription;
			NewNodeDescription.Name = InDescription.Name;
			NewNodeDescription.UniqueID = NewUniqueId;
			NewNodeDescription.ObjectTypeOfNode = EMetasoundClassType::Input;

			GraphPtr->Nodes.Add(NewNodeDescription);

			FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NewNodeDescription.UniqueID];
			FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NewNodeDescription.Name, OwningAsset };
			// todo: add special enum for input and output nodes
			return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
		}

		bool FGraphHandle::RemoveInput(const FString& InputName)
		{
			if (!IsValid())
			{
				return false;
			}

			TArray<FMetasoundInputDescription>& Inputs = GraphsClassDeclaration->Inputs;
			int32 IndexOfInputToRemove = -1;

			for (int32 InputIndex = 0; InputIndex < Inputs.Num(); InputIndex++)
			{
				if (Inputs[InputIndex].Name == InputName)
				{
					IndexOfInputToRemove = InputIndex;
					break;
				}
			}

			if (!ensureAlwaysMsgf(IndexOfInputToRemove >= 0, TEXT("Tried to remove an Input that didn't exist: %s"), *InputName))
			{
				return false;
			}

			// find the corresponding node handle to delete.
			FNodeHandle InputNode = GetInputNodeWithName(InputName);

			// If we found the input declared in the class description but couldn't find it in the graph,
			// something has gone terribly wrong. Remove the input from the description, but still ensure.
			if (!ensureAlwaysMsgf(InputNode.IsValid(), TEXT(R"(Couldn't find an input node with name %s, even though we found the input listed as a dependency.
				This indicates the underlying FMetasoundClassDescription is corrupted.
				Removing the Input in the class dependency to resolve...)"), *InputName))
			{
				Inputs.RemoveAt(IndexOfInputToRemove);
				return true;
			}

			// Finally, remove the node, and remove the input.
			if (!ensureAlwaysMsgf(RemoveNode(InputNode, true), TEXT("Call to RemoveNode failed.")))
			{
				return false;
			}

			Inputs.RemoveAt(IndexOfInputToRemove);
			return true;
		}

		FNodeHandle FGraphHandle::AddNewOutput(const FMetasoundOutputDescription& OutDescription)
		{
			if (!IsValid())
			{
				return FNodeHandle::InvalidHandle();
			}

			// @todo: verify that InDescription.TypeName is a valid Metasound type.

			const uint32 NewUniqueId = FindNewUniqueNodeId();

			if (!ensureAlwaysMsgf(NewUniqueId != INDEX_NONE, TEXT("FindNewUniqueNodeId failed")))
			{
				return FNodeHandle::InvalidHandle();
			}


			TArray<FMetasoundOutputDescription>& Outputs = GraphsClassDeclaration->Outputs;

			// Sanity check that this input has a unique name.
			for (const FMetasoundOutputDescription& Output : Outputs)
			{
				if (!ensureAlwaysMsgf(Output.Name != OutDescription.Name, TEXT("Tried to add a new output with a name that already exists!")))
				{
					return FNodeHandle::InvalidHandle();
				}
			}

			// Add the output to this node's class description.
			Outputs.Add(OutDescription);

			// Add a node for this output to the graph description.
			FMetasoundNodeDescription NewNodeDescription;
			NewNodeDescription.Name = OutDescription.Name;
			NewNodeDescription.UniqueID = NewUniqueId;
			NewNodeDescription.ObjectTypeOfNode = EMetasoundClassType::Output;

			GraphPtr->Nodes.Add(NewNodeDescription);

			FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NewNodeDescription.UniqueID];

			// todo: add special enum or class for input/output nodes
			FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NewNodeDescription.Name, OwningAsset };
			return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
		}

		bool FGraphHandle::RemoveOutput(const FString& OutputName)
		{
			if (!IsValid())
			{
				return false;
			}

			TArray<FMetasoundOutputDescription>& Outputs = GraphsClassDeclaration->Outputs;
			int32 IndexOfOutputToRemove = -1;

			for (int32 OutputIndex = 0; OutputIndex < Outputs.Num(); OutputIndex++)
			{
				if (Outputs[OutputIndex].Name == OutputName)
				{
					IndexOfOutputToRemove = OutputIndex;
					break;
				}
			}

			if (!ensureAlwaysMsgf(IndexOfOutputToRemove >= 0, TEXT("Tried to remove an Output that didn't exist: %s"), *OutputName))
			{
				return false;
			}

			// find the corresponding node handle to delete.
			FNodeHandle OutputNode = GetOutputNodeWithName(OutputName);

			// If we found the input declared in the class description but couldn't find it in the graph,
			// something has gone terribly wrong. Remove the input from the description, but still ensure.
			if (!ensureAlwaysMsgf(OutputNode.IsValid(), TEXT(R"(Couldn't find an input node with name %s, even though we found the input listed as a dependency.
				This indicates the underlying FMetasoundClassDescription is corrupted.
				Removing the Input in the class dependency to resolve...)"), *OutputName))
			{
				Outputs.RemoveAt(IndexOfOutputToRemove);
				return true;
			}

			// Finally, remove the node, and remove the input.
			if (!ensureAlwaysMsgf(RemoveNode(OutputNode, true), TEXT("Call to RemoveNode failed.")))
			{
				return false;
			}

			Outputs.RemoveAt(IndexOfOutputToRemove);
			return true;
		}

		FNodeHandle FGraphHandle::AddNewNode(const FNodeClassInfo& InNodeClass)
		{
			if (!IsValid())
			{
				return FNodeHandle::InvalidHandle();
			}

			// First, scan our dependency list to see if this node already exists there, and if not, get it.
			TArray<FMetasoundClassDescription>& Dependencies = OwningDocument->Dependencies;

			bool bFoundMatchingDependencyInDocument = false;

			for (FMetasoundClassDescription& Dependency : Dependencies)
			{
				if (Dependency.Metadata.NodeName == InNodeClass.NodeName && Dependency.Metadata.NodeType == InNodeClass.NodeType)
				{
					bFoundMatchingDependencyInDocument = true;

					// If this dependency was in the document's dependency list, check to see if we need to add it to this class' dependencies.
					bool bFoundDependencyInLocalClass = false;
					for (uint32 DependencyID : GraphsClassDeclaration->DependencyIDs)
					{
						if (DependencyID == Dependency.UniqueID)
						{
							bFoundDependencyInLocalClass = true;
							break;
						}
					}

					if (!bFoundDependencyInLocalClass)
					{
						// This dependency is already referenced somewhere in the document, but not for this graph's class. Add it.
						GraphsClassDeclaration->DependencyIDs.Add(Dependency.UniqueID);
						UE_LOG(LogTemp, Verbose, TEXT("Adding %s as a dependency for Metasound graph %s in Document %s"), *InNodeClass.NodeName, *GetGraphMetadata().NodeName, *OwningDocument->RootClass.Metadata.NodeName);
					}

					break;
				}
			}

			

			// If we haven't added a node of this class to the graph yet, add it to the dependencies for this class.
			if (!bFoundMatchingDependencyInDocument)
			{
				FMetasoundClassDescription NewDependencyClassDescription = GetClassDescriptionForNode(InNodeClass);
				NewDependencyClassDescription.UniqueID = FindNewUniqueDependencyId();
				Dependencies.Add(NewDependencyClassDescription);
				GraphsClassDeclaration->DependencyIDs.Add(NewDependencyClassDescription.UniqueID);

				UE_LOG(LogTemp, Verbose, TEXT("Adding %s is used in graph %s, adding as a new dependency for Metasound Document %s"), *InNodeClass.NodeName, *GetGraphMetadata().NodeName, *OwningDocument->RootClass.Metadata.NodeName);
			}

			// Add a new node instance for this class.

			const uint32 NewUniqueId = FindNewUniqueNodeId();
			if (!ensureAlwaysMsgf(NewUniqueId != INDEX_NONE, TEXT("Call to FindNewUniqueNodeId failed!")))
			{
				return FNodeHandle::InvalidHandle();
			}

			// Add a node for this output to the graph description.
			FMetasoundNodeDescription NewNodeDescription;
			NewNodeDescription.Name = InNodeClass.NodeName;
			NewNodeDescription.UniqueID = NewUniqueId;
			NewNodeDescription.ObjectTypeOfNode = InNodeClass.NodeType;

			GraphPtr->Nodes.Add(NewNodeDescription);

			FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NewNodeDescription.UniqueID];
			FHandleInitParams InitParams = { GraphPtr.GetAccessPoint(), NodePath, NewNodeDescription.Name, OwningAsset };
			return FNodeHandle(FHandleInitParams::PrivateToken, InitParams);
		}

		bool FGraphHandle::RemoveNode(FNodeHandle& InNode, bool bEvenIfInputOrOutputNode /*= false*/)
		{
			if (!IsValid())
			{
				return false;
			}

			// First, find the node in our nodes list,
			// while also checking to see if this is the only node of this class left in this graph.
			TArray<FMetasoundNodeDescription>& Nodes = GraphPtr->Nodes;
			int32 IndexOfNodeToRemove = -1;
			int32 NodesOfClass = 0;

			FString NodeClassName = InNode.GetNodeClassName();

			for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); NodeIndex++)
			{
				if (Nodes[NodeIndex].Name == NodeClassName)
				{
					NodesOfClass++;
				}
				
				if (Nodes[NodeIndex].UniqueID == InNode.GetNodeID())
				{
					IndexOfNodeToRemove = NodeIndex;
				}

				// If we've found the matching node,
				// and have found that there is more than one node of this class,
				// we have found all the info we need.
				if (IndexOfNodeToRemove > 0 && NodesOfClass > 1)
				{
					break;
				}
			}

			if (!ensureAlwaysMsgf(IndexOfNodeToRemove >= 0, TEXT(R"(Couldn't find node corresponding to handle (%s ID: %u).
				Are you sure this FNodeHandle was generated from this FGraphHandle?)"),
				*InNode.GetNodeClassName(),
				InNode.GetNodeType()))
			{
				return false;
			}

			// This should never hit based on the logic above.
			if (!ensureAlwaysMsgf(NodesOfClass > 0, TEXT("Found node with matching ID (%u) but mismatched class (%s). Likely means that the underlying class description was corrupted."), 
				*InNode.GetNodeClassName(),
				InNode.GetNodeType()))
			{
				return false;
			}

			// If this node was the only node of this class remaining in the graph,
			// Remove its ID as a dependency for the graph.
			if (NodesOfClass < 2)
			{
				TArray<uint32>& DependencyIDs = GraphsClassDeclaration->DependencyIDs;
				int32 IndexOfDependencyToRemove = -1;

				uint32 UniqueIDForThisDependency = INDEX_NONE;
				TArray<FMetasoundClassDescription>& DependencyClasses = OwningDocument->Dependencies;
				
				// scan the owning document's depenency classes for a dependency with this name.
				int32 IndexOfDependencyInDocument = -1;
				for (int32 Index = 0; Index < DependencyClasses.Num(); Index++)
				{
					if (DependencyClasses[Index].Metadata.NodeName == NodeClassName)
					{
						IndexOfDependencyInDocument = Index;
						UniqueIDForThisDependency = DependencyClasses[Index].UniqueID;
						break;
					}
				}

				if (!ensureAlwaysMsgf(IndexOfDependencyToRemove >= 0, 
					TEXT("Couldn't find node class %s in the list of dependencies for this document, but found it in the nodes list. This likely means that the underlying class description is corrupted.)"), *NodeClassName))
				{
					return false;
				}

				for (int32 DependencyIndex = 0; DependencyIndex < DependencyIDs.Num(); DependencyIndex++)
				{
					const bool bIsDependencyForNodeToRemove = DependencyIDs[DependencyIndex] == UniqueIDForThisDependency;

					if (bIsDependencyForNodeToRemove)
					{
						IndexOfDependencyToRemove = DependencyIndex;
						break;
					}
				}

				if (ensureAlwaysMsgf(IndexOfDependencyToRemove > 0, TEXT(R"(Couldn't find node class %s in the list of dependencies for this graph, but found it in the nodes list.
					This likely means that the underlying class description is corrupted.)"), *NodeClassName))
				{
					DependencyIDs.RemoveAt(IndexOfDependencyToRemove);
				}

				// Finally, check to see if there are any classes remaining in this document that depend on this class, and remove it from our dependencies list.
				bool bFoundUsageOfDependencyInClass = false;

				for (FMetasoundClassDescription& Dependency : DependencyClasses)
				{
					if (Dependency.DependencyIDs.Contains(UniqueIDForThisDependency))
					{
						bFoundUsageOfDependencyInClass = true;
					}
				}

				// Also scan the root graph for this document, which lives outside of the Dependencies list.
				FMetasoundClassDescription& RootClass = OwningDocument->RootClass;
				if (RootClass.DependencyIDs.Contains(UniqueIDForThisDependency))
				{
					bFoundUsageOfDependencyInClass = true;
				}

				if (!bFoundUsageOfDependencyInClass)
				{
					// we can safely delete this dependency from the document.
					DependencyClasses.RemoveAt(IndexOfDependencyInDocument);
				}
			}

			// Finally, remove the node from the nodes list.
			Nodes.RemoveAt(IndexOfNodeToRemove);
			return true;
		}

		FMetasoundClassMetadata FGraphHandle::GetGraphMetadata()
		{
			if (!IsValid())
			{
				return FMetasoundClassMetadata();
			}

			return GraphsClassDeclaration->Metadata;
		}

		UMetasound* FGraphHandle::SaveToNewMetasoundAsset(const FString& InPath)
		{
			if (!IsValid())
			{
				return nullptr;
			}

			UMetasound* NewMetasoundNode = NewObject<UMetasound>();
			NewMetasoundNode->SetMetasoundDocument(OwningDocument.GetChecked());

			ensureAlwaysMsgf(false, TEXT("Implement the actual saving part!"));
			ensureAlwaysMsgf(false, TEXT("Decide whether to delete this graph from the asset that owns it!"));

			return NewMetasoundNode;
		}


		bool FGraphHandle::ExportToJSONAsset(const FString& InAbsolutePath)
		{
			if (!IsValid())
			{
				return false;
			}

			if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
			{
				FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
				FStructSerializer::Serialize<FMetasoundClassDescription>(GraphsClassDeclaration.GetChecked(), Backend);
				FileWriter->Close();

				return true;
			}
			else
			{
				ensureAlwaysMsgf(false, TEXT("Failed to create a filewriter with the given path."));
				return false;
			}
		}

		bool FGraphHandle::InflateNodeDirectlyIntoGraph(FNodeHandle& InNode)
		{
			ensureAlwaysMsgf(false, TEXT("Implement me!"));
			// nontrivial but required anyways for graph inflation (UEAU-475)

			//step 0: get the node's FMetasoundClassDescription
			//step 1: check if the node is itself a metasound
			//step 2: get the FMetasoundGraphDescription for the node from the Dependencies list.
			//step 3: create new unique IDs for each node in the subgraph.
			//step 4: add nodes from subgraph to the current graph.
			//step 5: rebuild connections for new nodes in current graph based on the new IDs.
			//step 6: delete Input nodes and Output nodes from the subgraph, and rebuild connections from this graph directly to the nodes in the subgraph.
			return false;
		}

		TTuple<FGraphHandle, FNodeHandle> FGraphHandle::CreateEmptySubgraphNode(const FMetasoundClassMetadata& InInfo)
		{
			auto BuildInvalidTupleHandle = [&]() -> TTuple<FGraphHandle, FNodeHandle>
			{
				return TTuple<FGraphHandle, FNodeHandle>(FGraphHandle::InvalidHandle(), FNodeHandle::InvalidHandle());
			};

			if (!IsValid())
			{
				return BuildInvalidTupleHandle();
			}

			// Sanity check that the given name isn't already in our graph's dependency list.
			TArray<FMetasoundClassDescription>& Dependencies = OwningDocument->Dependencies;
			for (FMetasoundClassDescription& Dependency : Dependencies)
			{
				if (!ensureAlwaysMsgf(Dependency.Metadata.NodeName != InInfo.NodeName, TEXT("Tried to create a new subgraph with name %s but there was already a dependency named that in the graph."), *InInfo.NodeName))
				{
					return BuildInvalidTupleHandle();
				}
			}

			// Create a new class in this graph's dependencies list:
			uint32 NewUniqueIDForGraph = FindNewUniqueDependencyId();
			if (!ensureAlwaysMsgf(NewUniqueIDForGraph != INDEX_NONE, TEXT("Call to FindNewUniqueNodeId failed!")))
			{
				return BuildInvalidTupleHandle();
			}

			FMetasoundClassDescription& NewGraphClass = Dependencies.Add_GetRef(FMetasoundClassDescription());
			NewGraphClass.Metadata = InInfo;
			NewGraphClass.Metadata.NodeType = EMetasoundClassType::MetasoundGraph;
			NewGraphClass.UniqueID = NewUniqueIDForGraph;

			// Add the new subgraph's ID as a dependency for the current graph:
			GraphsClassDeclaration->DependencyIDs.Add(NewUniqueIDForGraph);

			// Generate a new FGraphHandle for this subgraph:
			FDescPath PathForNewGraph = FDescPath()[Path::EFromDocument::ToDependencies][*InInfo.NodeName][Path::EFromClass::ToGraph];
			FHandleInitParams InitParams = { GraphsClassDeclaration.GetAccessPoint(), PathForNewGraph, InInfo.NodeName, OwningAsset };
			FGraphHandle SubgraphHandle = FGraphHandle(FHandleInitParams::PrivateToken, InitParams);

			// Create the node for this subgraph in the current graph:
			const uint32 NewUniqueId = FindNewUniqueNodeId();
			if (!ensureAlwaysMsgf(NewUniqueId != INDEX_NONE, TEXT("Call to FindNewUniqueNodeId failed!")))
			{
				return BuildInvalidTupleHandle();
			}

			// Add a node for this output to the graph description.
			FMetasoundNodeDescription NewNodeDescription;
			NewNodeDescription.Name = InInfo.NodeName;
			NewNodeDescription.UniqueID = NewUniqueId;
			NewNodeDescription.ObjectTypeOfNode = InInfo.NodeType;

			GraphPtr->Nodes.Add(NewNodeDescription);

			FDescPath NodePath = GraphPtr.GetPath()[Path::EFromGraph::ToNodes][NewNodeDescription.UniqueID];
			FHandleInitParams NodeInitParams = { GraphPtr.GetAccessPoint(), NodePath, InInfo.NodeName, OwningAsset };
			FNodeHandle SubgraphNode = FNodeHandle(FHandleInitParams::PrivateToken, NodeInitParams);

			return TTuple<FGraphHandle, FNodeHandle>(SubgraphHandle, SubgraphNode);
		}

		TUniquePtr<Metasound::IOperator> FGraphHandle::BuildOperator()
		{
			// @todo: bring over operator builder from previous frontend draft with this new implementation.
			ensureAlwaysMsgf(false, TEXT("Implement Me!"));
			return nullptr;
		}

		FGraphHandle GetGraphHandleForClass(const FMetasoundClassDescription& InClass)
		{
			ensureAlwaysMsgf(false, TEXT("Implement Me!"));

			// to implement this, we'll need to
			// step 1. add tags for metasound asset UObject types that make their node name/inputs/outputs asset registry searchable 
			// step 2. search the asset registry for the assets.
			// step 3. Consider a runtime implementation for this using soft object paths.
			return FGraphHandle::InvalidHandle();
		}
	}
}

IMPLEMENT_MODULE(FDefaultModuleImpl, MetasoundFrontend);
