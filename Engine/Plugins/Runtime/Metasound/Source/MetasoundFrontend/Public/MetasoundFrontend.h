// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundGraph.h"
#include "MetasoundFrontendDataLayout.h"
#include "MetasoundFrontendBaseClasses.h"


class UMetasound;

namespace Metasound
{	
	/**
	 * These classes are used for editing Metasounds.
	 * In general, the workflow for editing a Metasound graph will be:
	 * 1) Load or create a metasound asset.
	 * 2) Call UMetasound::GetGraphHandle() to get a handle to the graph for that Metasound asset.
	 * 
	 * All Metasound documents are saved as a FMetasoundClassDescription, which itself can own FMetasoundClassDescriptions in a tree-like hierarchy.
	 * Typically the workflow for creating a Metasound subgraph will be
	 * 1) Get a Metasound::Frontend::FGraphHandle (typically through the two steps described in the first paragraph)
	 * 2) Build a FMetasoundClassMetadata struct with whatever name/author/description you want to give the subgraph,
	 * 3) call Metasound::Frontend::FGraphHandle::CreateEmptySubgraphNode, which will return that subgraph as it's own FGraphHandle,
	 *    as well as the FNodeHandle for that subgraph in the current graph.
	 *
	 * If you want to bounce that subgraph to it's own saved Metasound Asset, call FGraphHandle::SaveToNewMetasoundAsset on that subgraph's FGraphHandle.
	 *
	 * General note- these apis are NOT thread safe. 
	 * Make sure that any FGraphHandle, FNodeHandle, FInputHandle and FOutputHandle that access similar data are called on the same thread.
	 */
	namespace Frontend
	{

		// Struct with the basics of a node class' information,
		// used to look up that node from our node browser functions,
		// and also used in FGraphHandle::AddNewNode.
		struct FNodeClassInfo
		{
			FString NodeName;
			EMetasoundClassType NodeType;

		};

		// Get all available nodes of any type.
		TArray<FNodeClassInfo> GetAllAvailableNodeClasses();

		// Get all possible nodes whose name begins with a specific namespace.
		TArray<FNodeClassInfo> GetAllNodeClassesInNamespace(const FString& InNamespace);

		// Similar to GetAllNodeClassesInNamespace, except searches all nodes for a match with a substring.
		TArray<FNodeClassInfo> GetAllNodesWhoseNameContains(const FString& InSubstring);

		// Searches for any node types that can output a specific type.
		TArray<FNodeClassInfo> GetAllNodesWithAnOutputOfType(const FName& InType);

		TArray<FNodeClassInfo> GetAllNodesWithAnInputOfType(const FName& InType);

		// gets all metadata (name, description, author, what to say if it's missing) for a given node.
		FMetasoundClassMetadata GetMetadataForNode(const FNodeClassInfo& InInfo);


		// Generates a new FMetasoundClassDescription for a given node class. Only used by classes that manipulate Metasound Description data directly.
		FMetasoundClassDescription GetClassDescriptionForNode(const FNodeClassInfo& InInfo);

		// Struct that indicates whether an input and an output can be connected,
		// and whether an intermediate node is necessary to connect the two.
		struct FConnectability
		{
			enum class EConnectable
			{
				Yes,
				No,
				YesWithConverterNode
			};

			EConnectable Connectable;

			// If Connectable is EConnectable::YesWithConverterNode,
			// this will be a populated list of nodes we can use 
			// to convert between the input and output.
			TArray<FNodeClassInfo> PossibleConverterNodeClasses;
		};

		class FInputHandle;

		struct FHandleInitParams
		{
			TWeakPtr<FDescriptionAccessPoint> InAccessPoint;

			// Path in the document to the element we're getting a handle to.
			// for the FGraphHandle, this is a path to the graph within the graph's Class description.
			// for the FNodeHandle, FInputHandle and FOutputHandle, this is a path to the individual node within a Graph description.
			const FDescPath& InPath;

			// Class name for the graph or node to get a handle for.
			const FString& InClassName;

			// The asset that owns the FMetasoundDocument this handle is associated with.
			TWeakObjectPtr<UObject> InOwningAsset;

		private:
			// This is used to grant specific classes the ability to create handles.
			enum EPrivateToken { Token };
			static const EPrivateToken PrivateToken = EPrivateToken::Token;

			friend class FOutputHandle;
			friend class FInputHandle;
			friend class FNodeHandle;
			friend class FGraphHandle;
			friend class ::UMetasound;
		};

		class FOutputHandle : protected ITransactable
		{
		public:
			FOutputHandle() = delete;

			explicit FOutputHandle(FHandleInitParams::EPrivateToken InToken, const FHandleInitParams& InParams, const FString& InOutputName);

			~FOutputHandle() = default;

			static FOutputHandle InvalidHandle();

			bool IsValid() const;

			// @todo: with the current spec, we'd have to scan all nodes in the graph for this output to solve this.
			// Is this worth it, or should we just require folks to use FInputHandle::IsConnected()?
			//bool IsConnected() const;

			FName GetOutputType() const;
			FString GetOutputName() const;
			FString GetOutputTooltip() const;
			uint32 GetOwningNodeID() const;

			// @todo: with the current spec, we'd have to scan all nodes in the graph for this output to solve this.
			// Is this worth it, or should we just require folks to use FInputHandle::GetCurrentlyConnectedOutput()?
			//FInputHandle GetCurrentlyConnectedInput();

			FConnectability CanConnectTo(const FInputHandle& InHandle) const;
			bool Connect(FInputHandle& InHandle);
			bool ConnectWithConverterNode(FInputHandle& InHandle, FString& InNodeClassName);

		private:
			TDescriptionPtr<FMetasoundNodeDescription> NodePtr;
			TDescriptionPtr<FMetasoundClassDescription> NodeClass;
			TDescriptionPtr<FMetasoundOutputDescription> OutputPtr;
		};

		class FInputHandle : protected ITransactable
		{
		public:
			FInputHandle() = delete;
			~FInputHandle() = default;

			// Sole constructor for FInputHandle. Can only be used by friend classes for FHandleInitParams.
			explicit FInputHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams, const FString& InputName);

			static FInputHandle InvalidHandle();

			bool IsValid() const;
			bool IsConnected() const;

			FName GetInputType() const;
			FString GetInputName() const;
			FString GetInputTooltip() const;

			FOutputHandle GetCurrentlyConnectedOutput() const;

			FConnectability CanConnectTo(const FOutputHandle& InHandle) const;
			bool Connect(FOutputHandle& InHandle);
			bool ConnectWithConverterNode(FOutputHandle& InHandle, FString& InNodeClassName);

		private:
			TDescriptionPtr<FMetasoundNodeDescription> NodePtr;
			TDescriptionPtr<FMetasoundClassDescription> NodeClass;
			TDescriptionPtr<FMetasoundInputDescription> InputPtr;

			FString InputName;

			const FMetasoundNodeConnectionDescription* GetConnectionDescription() const;
			FMetasoundNodeConnectionDescription* GetConnectionDescription();
		};

		class FGraphHandle;

		// Opaque handle to a single node on a graph.
		// Can retrieve metadata about that node,
		// get FNodeHandles for other nodes it is connected to, and validate and modify connections.
		class FNodeHandle : protected ITransactable
		{
		public:
			FNodeHandle() = delete;
			~FNodeHandle() = default;

			// Sole constructor for FNodeHandle. Can only be used by friends of FHandleInitParams.
			explicit FNodeHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams);

			static FNodeHandle InvalidHandle();

			bool IsValid() const;

			TArray<FInputHandle> GetAllInputs();
			TArray<FOutputHandle> GetAllOutputs();

			FInputHandle GetInputWithName(const FString& InName);
			FOutputHandle GetOutputWithName(const FString& InName);

			EMetasoundClassType GetNodeType();
			FString GetNodeClassName();

			// If this node is itself a Metasound,
			// use this to return the contained graph for the metasound.
			// Otherwise it will return an invalid FGraphHandle.
			void GetContainedGraph(FGraphHandle& OutGraph);

			uint32 GetNodeID();
			static uint32 GetNodeID(const FDescPath& InNodePath);

		private:

			TDescriptionPtr<FMetasoundNodeDescription> NodePtr;
			TDescriptionPtr<FMetasoundClassDescription> NodeClass;

			uint32 NodeID;
		};

		class METASOUNDFRONTEND_API FGraphHandle : protected ITransactable
		{
		public:
			FGraphHandle() = delete;
			~FGraphHandle() = default;

			// Sole constructor for FGraphHandle. Can only be used by friends of FHandleInitParams.
			FGraphHandle(FHandleInitParams::EPrivateToken PrivateToken, const FHandleInitParams& InParams);

			static FGraphHandle InvalidHandle();

			bool IsValid();

			TArray<FNodeHandle> GetAllNodes();
			TArray<FNodeHandle> GetOutputNodes();
			TArray<FNodeHandle> GetInputNodes();

			FNodeHandle GetOutputNodeWithName(const FString& InName);
			FNodeHandle GetInputNodeWithName(const FString& InName);

			FNodeHandle AddNewInput(const FMetasoundInputDescription& InDescription);
			bool RemoveInput(const FString& InputName);

			FNodeHandle AddNewOutput(const FMetasoundOutputDescription& OutDescription);
			bool RemoveOutput(const FString& OutputName);

			FNodeHandle AddNewNode(const FNodeClassInfo& InNodeClass);

			// Remove the node corresponding to this node handle.
			// On success, invalidates the received node handle.
			bool RemoveNode(FNodeHandle& InNode, bool bEvenIfInputOrOutputNode = false);

			// Returns the metadata for the current graph, including the name, description and author.
			FMetasoundClassMetadata GetGraphMetadata();

			// Exports this graph to a JSON at the given path.
			// @returns true on success.
			bool ExportToJSONAsset(const FString& InAbsolutePath);

			// If the FNodeHandle given is itself a Metasound graph,
			// and the FNodeHandle is a direct member of this FGraphHandle,
			// If successful, this will invalidate the FNodeHandle and paste the graph for this node directly
			// into the graph.
			// If not successful, InNode will not be affected.
			// @returns true on success, false on failure.
			bool InflateNodeDirectlyIntoGraph(FNodeHandle& InNode);

			// When called, creates a new node in this graph that can be edited directly with the FGraphHandle
			// that gets returned.
			// @param InInfo the name, author, etc that should be used for this graph.
			// @returns a tuple of the FGraphHandle for this subgraph (which can be independently manipulated),
			//          as well as the FNodeHandle for the subgraph's node in this graph.
			//          Both of these will be invalid on failure.
			TTuple<FGraphHandle, FNodeHandle> CreateEmptySubgraphNode(const FMetasoundClassMetadata& InInfo);

			// This invokes the Metasound Builder to synchronously compile a 
			// metasound operator, which can then be used for playback.
			// @returns nullptr on failure.
			// @todo: forward errors from the Metasound Builder.
			// @todo: make an API for doing this async.
			TUniquePtr<IOperator> BuildOperator();

		private:

			// The graph struct itself.
			TDescriptionPtr<FMetasoundGraphDescription> GraphPtr;

			// The class description for the graph corresponding to this.
			TDescriptionPtr<FMetasoundClassDescription> GraphsClassDeclaration;

			// the outermost document for all dependencies for the document this graph lives in.
			TDescriptionPtr<FMetasoundDocument> OwningDocument;

			// Scans all existing node ids to guarantee a new unique ID.
			uint32 FindNewUniqueNodeId();

			// Scans all existing dependency IDs in the root dependency list to guarantee a new unique ID when adding a dependency.
			uint32 FindNewUniqueDependencyId();
		};

		// If there's a Metasound node referenced by a class in it's dependencies list,
		// but the node' graph isn't implemented in that dependency list,
		// this can be used to attempt to find which asset that node is implemented in,
		// and return a graph handle for that node.
		FGraphHandle GetGraphHandleForClass(const FMetasoundClassDescription& InClass);
	}	
}
