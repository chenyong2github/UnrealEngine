// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundGraph.h"

/* Metasound Controllers and Handles provide a object oriented interface for  manipulating Metasound Documents. 
 *
 * Each controller interface is associated with a single Metasound entity such as 
 * Document, Graph, Node, Input or Output. Access to these entities generally begins
 * with the DocumentController which provides access to Graphs. Graphs provide access
 * to Nodes and Nodes provide access to Inputs and Outputs. 
 *
 * A "Handle" is simply a TSharedRef<> of a controller.
 *
 * In general, the workflow for editing a Metasound graph will be:
 * 1) Load or create a metasound asset.
 * 2) Call UMetaSound::GetDocumentHandle() to get a handle to the document for that asset.
 * 
 * Typically the workflow for creating a Metasound subgraph will be
 * 1) Get a Metasound::Frontend::FGraphHandle (typically through the two steps described in the first paragraph)
 * 2) Build a FMetasoundFrontendClassMetadata struct with whatever name/author/description you want to give the subgraph,
 * 3) call Metasound::Frontend::FGraphHandle::CreateEmptySubgraphNode, which will return that subgraph as it as a FNodeHandle for that subgraph in the current graph.
 * 4) class Metasound::Frontend::FNodeHandle::AsGraph which provides access to edit the subgraphs internal structure as well as externally facing inputs and outputs.
 *
 * General note- these apis are NOT thread safe. 
 * Make sure that any FDocumentHandle, FGraphHandle, FNodeHandle, FInputHandle and FOutputHandle that access similar data are called on the same thread.
 */
namespace Metasound
{
	namespace Frontend
	{
		// Forward declare 
		class IInputController;
		class IOutputController;
		class INodeController;
		class IGraphController;
		class IDocumentController;

		// Metasound Frontend Handles are all TSharedRefs of various Metasound Frontend Controllers.
		using FInputHandle = TSharedRef<IInputController>;
		using FConstInputHandle = TSharedRef<const IInputController>;
		using FOutputHandle = TSharedRef<IOutputController>;
		using FConstOutputHandle = TSharedRef<const IOutputController>;
		using FNodeHandle = TSharedRef<INodeController>;
		using FConstNodeHandle = TSharedRef<const INodeController>;
		using FGraphHandle = TSharedRef<IGraphController>;
		using FConstGraphHandle = TSharedRef<const IGraphController>;
		using FDocumentHandle = TSharedRef<IDocumentController>;
		using FConstDocumentHandle = TSharedRef<const IDocumentController>;


		// Container holding various access pointers to the FMetasoundFrontendDocument
		struct FConstDocumentAccess
		{
			FConstVertexAccessPtr ConstVertex;
			FConstClassInputAccessPtr ConstClassInput;
			FConstClassOutputAccessPtr ConstClassOutput;
			FConstNodeAccessPtr ConstNode;
			FConstClassAccessPtr ConstClass;
			FConstGraphClassAccessPtr ConstGraphClass;
			FConstGraphAccessPtr ConstGraph;
			FConstDocumentAccessPtr ConstDocument;
		};

		// Container holding various access pointers to the FMetasoundFrontendDocument
		struct FDocumentAccess : public FConstDocumentAccess
		{
			FVertexAccessPtr Vertex; 
			FClassInputAccessPtr ClassInput;
			FClassOutputAccessPtr ClassOutput;
			FNodeAccessPtr Node;
			FClassAccessPtr Class;
			FGraphClassAccessPtr GraphClass;
			FGraphAccessPtr Graph;
			FDocumentAccessPtr Document;
		};


		/** IDocumentAccessor describes an interface for various I*Controllers to interact with
		 * each other without exposing that interface publicly or requiring friendship 
		 * between various controller implementation classes. 
		 */
		class METASOUNDFRONTEND_API IDocumentAccessor
		{
			protected:
				/** Share access to FMetasoundFrontendDocument objects. 
				 *
				 * Derived classes must implement this method. In the implementation,
				 * derived classes should set the various TAccessPtrs on FDocumentAccess 
				 * to the TAccessPtrs held internal in the derived class.
				 *
				 * Sharing access simplifies operations involving multiple frontend controllers
				 * by providing direct access to the FMetasoundFrontendDocument objects to be
				 * edited. 
				 */
				virtual FDocumentAccess ShareAccess() = 0;

				/** Share access to FMetasoundFrontendDocument objects. 
				 *
				 * Derived classes must implement this method. In the implementation,
				 * derived classes should set the various TAccessPtrs on FDocumentAccess 
				 * to the TAccessPtrs held internal in the derived class.
				 *
				 * Sharing access simplifies operations involving multiple frontend controllers
				 * by providing direct access to the FMetasoundFrontendDocument objects to be
				 * edited. 
				 */
				virtual FConstDocumentAccess ShareAccess() const = 0;

				/** Returns the shared access from an IDocumentAccessor. */
				static FDocumentAccess GetSharedAccess(IDocumentAccessor& InDocumentAccessor);

				/** Returns the shared access from an IDocumentAccessor. */
				static FConstDocumentAccess GetSharedAccess(const IDocumentAccessor& InDocumentAccessor);
		};
		
		/* An IOutputController provides methods for querying and manipulating a metasound output vertex. */
		class METASOUNDFRONTEND_API IOutputController : public TSharedFromThis<IOutputController>, public IDocumentAccessor
		{
		public:
			static FOutputHandle GetInvalidHandle();

			IOutputController() = default;
			virtual ~IOutputController() = default;

			/** Returns true if the controller is in a valid state. */
			virtual bool IsValid() const = 0;

			virtual FGuid GetID() const = 0;
			
			/** Returns the data type name associated with this output. */
			virtual const FName& GetDataType() const = 0;
			
			/** Returns the name associated with this output. */
			virtual const FString& GetName() const = 0;
			
			/** Returns the human readable name associated with this output. */
			virtual const FText& GetDisplayName() const = 0;
			
			/** Returns the tooltip associated with this output. */
			virtual const FText& GetTooltip() const = 0;

			/** Returns all metadata associated with this output. */
			virtual const FMetasoundFrontendVertexMetadata& GetMetadata() const = 0;

			/** Returns the ID of the node which owns this output. */
			virtual FGuid GetOwningNodeID() const = 0;
			
			/** Returns a FNodeHandle to the node which owns this output. */
			virtual FNodeHandle GetOwningNode() = 0;
			
			/** Returns a FConstNodeHandle to the node which owns this output. */
			virtual FConstNodeHandle GetOwningNode() const = 0;

			/** Returns the currently connected output. If this input is not
			 * connected, the returned handle will be invalid. */
			virtual TArray<FInputHandle> GetCurrentlyConnectedInputs() = 0;

			/** Returns the currently connected output. If this input is not
			 * connected, the returned handle will be invalid. */
			virtual TArray<FConstInputHandle> GetCurrentlyConnectedInputs() const = 0;

			virtual bool Disconnect() = 0;

			/** Returns information describing connectability between this output and the supplied input. */
			virtual FConnectability CanConnectTo(const IInputController& InController) const = 0;
			
			/** Connects this output and the supplied input. 
			 * @return True on success, false on failure. 
			 */
			virtual bool Connect(IInputController& InController) = 0;
			
			/** Connects this output to the input using a converter node.
			 * @return True on success, false on failure. 
			 */
			virtual bool ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName) = 0;
			
			/** Disconnects this output from the input. 
			 * @return True on success, false on failure. 
			 */
			virtual bool Disconnect(IInputController& InController) = 0;

		};

		/* An IInputController provides methods for querying and manipulating a metasound input vertex. */
		class METASOUNDFRONTEND_API IInputController : public TSharedFromThis<IInputController>, public IDocumentAccessor
		{
		public:
			static FInputHandle GetInvalidHandle();

			IInputController() = default;
			virtual ~IInputController() = default;

			/** Returns true if the controller is in a valid state. */
			virtual bool IsValid() const = 0;

			virtual FGuid GetID() const = 0;

			/** Return true if the input is connect to an output. */
			virtual bool IsConnected() const = 0;

			/** Returns the data type name associated with this input. */
			virtual const FName& GetDataType() const = 0;

			/** Returns the data type name associated with this input. */
			virtual const FString& GetName() const = 0;

			/** Returns the data type name associated with this input. */
			virtual const FText& GetDisplayName() const = 0;

			/** Returns the default value of the given input. */
			virtual const FMetasoundFrontendLiteral* GetDefaultLiteral() const = 0;

			/** Returns the data type name associated with this input. */
			virtual const FText& GetTooltip() const = 0;

			/** Returns all metadata associated with this input. */
			virtual const FMetasoundFrontendVertexMetadata& GetMetadata() const = 0;

			/** Returns the ID of the node which owns this output. */
			virtual FGuid GetOwningNodeID() const = 0;
			
			/** Returns a FNodeHandle to the node which owns this output. */
			virtual FNodeHandle GetOwningNode() = 0;
			
			/** Returns a FConstNodeHandle to the node which owns this output. */
			virtual FConstNodeHandle GetOwningNode() const = 0;
			
			/** Returns the currently connected output. If this input is not
			 * connected, the returned handle will be invalid. */
			virtual FOutputHandle GetCurrentlyConnectedOutput() = 0;

			/** Returns the currently connected output. If this input is not
			 * connected, the returned handle will be invalid. */
			virtual FConstOutputHandle GetCurrentlyConnectedOutput() const = 0;

			/** Returns information describing connectability between this input and the supplied output. */
			virtual FConnectability CanConnectTo(const IOutputController& InController) const = 0;

			/** Connects this input and the supplied output. 
			 * @return True on success, false on failure. 
			 */
			virtual bool Connect(IOutputController& InController) = 0;

			/** Connects this input to the output using a converter node.
			 * @return True on success, false on failure. 
			 */
			virtual bool ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName) = 0;

			/** Disconnects this input from the given output. 
			 * @return True on success, false on failure. 
			 */
			virtual bool Disconnect(IOutputController& InController) = 0;

			/** Disconnects this input from any connected output. 
			 * @return True on success, false on failure. 
			 */
			virtual bool Disconnect() = 0;

			/*
			virtual bool IsLiteralSet() const = 0;
			virtual FMetasoundFrontendLiteral GetLiteral() const = 0;
			virtual bool SetLiteral(const FMetasoundFrontendLiteral& InLiteral) = 0;
			*/


		};

		/* An INodeController provides methods for querying and manipulating a Metasound node. */
		class METASOUNDFRONTEND_API INodeController : public TSharedFromThis<INodeController>, public IDocumentAccessor
		{

		public:
			static FNodeHandle GetInvalidHandle();

			INodeController() = default;
			virtual ~INodeController() = default;

			/** Returns true if the controller is in a valid state. */
			virtual bool IsValid() const = 0;

			/** Returns all node inputs. */
			virtual TArray<FInputHandle> GetInputs() = 0;

			/** Returns all node inputs. */
			virtual TArray<FConstInputHandle> GetConstInputs() const = 0;

			/** Returns the display name of the given node (what to distinguish and label in visual arrays, such as context menus). */
			virtual const FText& GetDisplayName() const = 0;

			/** Sets the description of the node. */
			virtual void SetDescription(const FText& InDescription) = 0;

			/** Sets the display name of the node. */
			virtual void SetDisplayName(const FText& InDisplayName) = 0;

			/** Returns the title of the given node (what to label when displayed as visual node). */
			virtual const FText& GetDisplayTitle() const = 0;

			/** Iterate over inputs */
			virtual void IterateInputs(TUniqueFunction<void(FInputHandle)> InFunction) = 0;
			virtual void IterateConstInputs(TUniqueFunction<void(FConstInputHandle)> InFunction) const = 0;

			/** Returns number of node inputs. */
			virtual int32 GetNumInputs() const = 0;

			/** Iterate over outputs */
			virtual void IterateOutputs(TUniqueFunction<void(FOutputHandle)> InFunction) = 0;
			virtual void IterateConstOutputs(TUniqueFunction<void(FConstOutputHandle)> InFunction) const = 0;

			/** Returns number of node outputs. */
			virtual int32 GetNumOutputs() const = 0;

			virtual TArray<FInputHandle> GetInputsWithVertexName(const FString& InName) = 0;
			virtual TArray<FConstInputHandle> GetInputsWithVertexName(const FString& InName) const = 0;

			/** Returns all node outputs. */
			virtual TArray<FOutputHandle> GetOutputs() = 0;

			/** Returns all node outputs. */
			virtual TArray<FConstOutputHandle> GetConstOutputs() const = 0;

			virtual TArray<FOutputHandle> GetOutputsWithVertexName(const FString& InName) = 0;
			virtual TArray<FConstOutputHandle> GetOutputsWithVertexName(const FString& InName) const = 0;

			/** Returns true if node is required to satisfy the document archetype. */
			virtual bool IsRequired() const = 0;

			/** Returns an input with the given id.
			 *
			 * If the input does not exist, an invalid handle is returned.
			 */
			virtual FInputHandle GetInputWithID(FGuid InVertexID) = 0;

			/** Returns an input with the given name. 
			 *
			 * If the input does not exist, an invalid handle is returned.
			 */
			virtual FConstInputHandle GetInputWithID(FGuid InVertexID) const = 0;

			/** Returns an output with the given name. 
			 *
			 * If the output does not exist, an invalid handle is returned. 
			 */
			virtual FOutputHandle GetOutputWithID(FGuid InVertexID) = 0;

			/** Returns an output with the given name. 
			 *
			 * If the output does not exist, an invalid handle is returned. 
			 */
			virtual FConstOutputHandle GetOutputWithID(FGuid InVertexID) const = 0;

			virtual bool CanAddInput(const FString& InVertexName) const = 0;
			virtual FInputHandle AddInput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) = 0;
			virtual bool RemoveInput(FGuid InVertexID) = 0;

			// TODO: VertexID -> PinID? VertexID? SlotID? RefID? 
			virtual bool CanAddOutput(const FString& InVertexName) const = 0;
			virtual FInputHandle AddOutput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) = 0;
			virtual bool RemoveOutput(FGuid InVertexID) = 0;

			/** Returns associated node class data */
			virtual const FMetasoundFrontendClassInterface& GetClassInterface() const = 0;
			virtual const FMetasoundFrontendClassMetadata& GetClassMetadata() const = 0;
			virtual const FMetasoundFrontendInterfaceStyle& GetOutputStyle() const = 0;
			virtual const FMetasoundFrontendInterfaceStyle& GetInputStyle() const = 0;
			virtual const FMetasoundFrontendClassStyle& GetClassStyle() const = 0;

			/** Description of the given node. */
			virtual const FText& GetDescription() const = 0;

			/** If the node is also a graph, this returns a graph handle.
			 * If the node is not also a graph, it will return an invalid handle.
			 */
			virtual FGraphHandle AsGraph() = 0;

			/** If the node is also a graph, this returns a graph handle.
			 * If the node is not also a graph, it will return an invalid handle.
			 */
			virtual FConstGraphHandle AsGraph() const = 0;

			/** Returns the name of this node. */
			virtual const FString& GetNodeName() const = 0;

			virtual const FMetasoundFrontendNodeStyle& GetNodeStyle() const = 0;
			virtual void SetNodeStyle(const FMetasoundFrontendNodeStyle& InStyle) = 0;

			/** Returns the ID associated with this node. */
			virtual FGuid GetID() const = 0;

			/** Returns the ID associated with the node class. */
			virtual FGuid GetClassID() const = 0;

			/** Returns the class ID of the metasound class which owns this node. */
			virtual FGuid GetOwningGraphClassID() const = 0;

			/** Returns the graph which owns this node. */
			virtual FGraphHandle GetOwningGraph() = 0;

			/** Returns the graph which owns this node. */
			virtual FConstGraphHandle GetOwningGraph() const = 0;
		};

		/* An IGraphController provides methods for querying and manipulating a Metasound graph. */
		class METASOUNDFRONTEND_API IGraphController : public TSharedFromThis<IGraphController>, public IDocumentAccessor
		{
			public:
			static FGraphHandle GetInvalidHandle();

			IGraphController() = default;
			virtual ~IGraphController() = default;

			/** Returns true if the controller is in a valid state. */
			virtual bool IsValid() const = 0;

			virtual FGuid GetNewVertexID() const = 0;

			virtual TArray<FString> GetInputVertexNames() const = 0;
			virtual TArray<FString> GetOutputVertexNames() const = 0;

			/** Returns all nodes in the graph. */
			virtual TArray<FNodeHandle> GetNodes() = 0;

			/** Returns a node by NodeID. If the node does not exist, an invalid handle is returned. */
			virtual FConstNodeHandle GetNodeWithID(FGuid InNodeID) const = 0;

			/** Returns all nodes in the graph. */
			virtual TArray<FConstNodeHandle> GetConstNodes() const = 0;

			/** Returns a node by NodeID. If the node does not exist, an invalid handle is returned. */
			virtual FNodeHandle GetNodeWithID(FGuid InNodeID) = 0;

			/** Returns all output nodes in the graph. */
			virtual TArray<FNodeHandle> GetOutputNodes() = 0;

			/** Returns all output nodes in the graph. */
			virtual TArray<FConstNodeHandle> GetConstOutputNodes() const = 0;

			/** Returns all input nodes in the graph. */
			virtual TArray<FNodeHandle> GetInputNodes() = 0;

			/** Returns all input nodes in the graph. */
			virtual TArray<FConstNodeHandle> GetConstInputNodes() const = 0;

			/** Iterate over all input nodes with the given function. If ClassType is specified, only iterate over given type. */
			virtual void IterateNodes(TUniqueFunction<void(FNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) = 0;

			/** Iterate over all nodes with the given function. If ClassType is specified, only iterate over given type. */
			virtual void IterateConstNodes(TUniqueFunction<void(FConstNodeHandle)> InFunction, EMetasoundFrontendClassType InClassType = EMetasoundFrontendClassType::Invalid) const = 0;

			/** Returns true if an output vertex with the given Name exists.
			 *
			 * @param InName - Name of vertex.
			 * @return True if the vertex exists, false otherwise. 
			 */
			virtual bool ContainsOutputVertexWithName(const FString& InName) const = 0;

			/** Returns true if an input vertex with the given Name exists.
			 *
			 * @param InName - Name of vertex.
			 * @return True if the vertex exists, false otherwise. 
			 */
			virtual bool ContainsInputVertexWithName(const FString& InName) const = 0;

			/** Returns a handle to an existing output node for the given graph output name.
			 * If no output exists with the given name, an invalid node handle is returned. 
			 *
			 * @param InName - Name of graph output.. 
			 * @return The node handle for the output node. If the output does not exist, an invalid handle is returned.
			 */
			virtual FNodeHandle GetOutputNodeWithName(const FString& InName) = 0;

			/** Returns a handle to an existing output node for the given graph output name.
			 * If no output exists with the given name, an invalid node handle is returned. 
			 *
			 * @param InName - Name of graph output.
			 * @return The node handle for the output node. If the node does not exist, an invalid handle is returned.
			 */
			virtual FConstNodeHandle GetOutputNodeWithName(const FString& InName) const = 0;

			/** Returns a handle to an existing input node for the given graph input name.
			 * If no input exists with the given name, an invalid node handle is returned. 
			 *
			 * @param InName - Name of graph input. 
			 * @return The node handle for the input node. If the node does not exist, an invalid handle is returned.
			 */
			virtual FNodeHandle GetInputNodeWithName(const FString& InName) = 0;

			/** Returns a handle to an existing input node for the given graph input name.
			 * If no input exists with the given name, an invalid node handle is returned. 
			 *
			 * @param InName - Name of graph input. 
			 * @return The node handle for the input node. If the node does not exist, an invalid handle is returned.
			 */
			virtual FConstNodeHandle GetInputNodeWithName(const FString& InName) const = 0;

			virtual FConstClassInputAccessPtr FindClassInputWithName(const FString& InName) const = 0;
			virtual FConstClassOutputAccessPtr FindClassOutputWithName(const FString& InName) const = 0;

			/** Add a new input node using the input description. 
			 *
			 * @param InDescription - Description for input of graph.
			 * @return On success, a valid input node handle. On failure, an invalid node handle.
			 */
			virtual FNodeHandle AddInputVertex(const FMetasoundFrontendClassInput& InDescription) = 0;

			/** Remove the input with the given name. Returns true if successfully removed, false otherwise. */
			virtual bool RemoveInputVertex(const FString& InputName) = 0;

			/** Add a new output node using the output description. 
			 *
			 * @param InDescription - Description for output of graph.
			 * @return On success, a valid output node handle. On failure, an invalid node handle.
			 */
			virtual FNodeHandle AddOutputVertex(const FMetasoundFrontendClassOutput& InDescription) = 0;

			/** Remove the output with the given name. Returns true if successfully removed, false otherwise. */
			virtual bool RemoveOutputVertex(const FString& OutputName) = 0;

			/** Returns the preferred literal argument type for a given input.
			 * Returns ELiteralType::Invalid if the input couldn't be found, 
			 * or if the input doesn't support any kind of literals.
			 *
			 * @param InInputName - Name of graph input.
			 * @return The preferred literal argument type. 
			 */
			virtual ELiteralType GetPreferredLiteralTypeForInputVertex(const FString& InInputName) const = 0;

			/** Return the UObject class corresponding an input. Meaningful for inputs whose preferred 
			 * literal type is UObject or UObjectArray.
			 *
			 * @param InInputName - Name of graph input.
			 *
			 * @return The UClass* for the literal argument input. nullptr on error or if UObject argument is not supported.
			 */
			virtual UClass* GetSupportedClassForInputVertex(const FString& InInputName) = 0;

			virtual FGuid GetVertexIDForInputVertex(const FString& InInputName) const = 0;
			virtual FGuid GetVertexIDForOutputVertex(const FString& InOutputName) const = 0;

			virtual FMetasoundFrontendLiteral GetDefaultInput(const FGuid& InVertexID) const = 0;
			virtual bool SetDefaultInput(const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral) = 0;

			/** Set the default value for the graph input.
			 *
			 * @param InInputName - Name of the graph input.
			 * @param InVertexID - Vertex to set to DataType default.
			 * @param InDataTypeName - Name of datatype to set to default.
			 *
			 * @return True on success. False if the input does not exist or if the literal type was incompatible with the input.
			 */
			virtual bool SetDefaultInputToDefaultLiteralOfType(const FGuid& InVertexID) = 0;

			/** Set the display name for the input with the given name. */
			virtual void SetInputDisplayName(const FString& InName, const FText& InDisplayName) = 0;

			/** Set the display name for the output with the given name. */
			virtual void SetOutputDisplayName(const FString& InName, const FText& InDisplayName) = 0;

			/** Get the description for the input with the given name. */
			virtual const FText& GetInputDescription(const FString& InName) const = 0;

			/** Get the description for the output with the given name. */
			virtual const FText& GetOutputDescription(const FString& InName) const = 0;

			/** Set the description for the input with the given name. */
			virtual void SetInputDescription(const FString& InName, const FText& InDescription) = 0;

			/** Set the description for the output with the given name. */
			virtual void SetOutputDescription(const FString& InName, const FText& InDescription) = 0;

			/** Clear the current literal for a given input.
			 *
			 * @return True on success, false on failure.
			 */
			virtual bool ClearLiteralForInput(const FString& InInputName, FGuid InVertexID) = 0;

			/** Add a new node to this graph.
			 *
			 * @param InNodeClass - Info for node class.
			 * 
			 * @return Node handle for class. On error, an invalid handle is returned. 
			 */
			virtual FNodeHandle AddNode(const FNodeClassInfo& InNodeClass) = 0;

			/** Add a new node to this graph.
			 *
			 * @param InNodeClass - Info for node class.
			 * 
			 * @return Node handle for class. On error, an invalid handle is returned. 
			 */
			virtual FNodeHandle AddNode(const FNodeRegistryKey& InNodeClass) = 0;

			/** Add a new node to this graph.
			 *
			 * @param InClassMetadat - Info for node class.
			 * 
			 * @return Node handle for class. On error, an invalid handle is returned. 
			 */
			virtual FNodeHandle AddNode(const FMetasoundFrontendClassMetadata& InClassMetadata) = 0;

			/** Remove the node corresponding to this node handle.
			 *
			 * @return True on success, false on failure. 
			 */
			virtual bool RemoveNode(INodeController& InNode) = 0;

			/** Returns the ClassID associated with this graph. */
			virtual FGuid GetClassID() const = 0;

			/** Return the metadata for the current graph. */
			virtual const FMetasoundFrontendClassMetadata& GetGraphMetadata() const = 0;

			/** Inflates a subgraph of this graph into the this graph.
			 *
			 * If the INodeController given is itself a Metasound graph,
			 * and the INodeController is a direct member of this IGraphController,
			 * this will invalidate the FNodeController and paste the graph for this 
			 * node directly into this graph.
			 *
			 * If not successful, InNode will not be affected.
			 *
			 * @returns True on success, false on failure.
			 */
			virtual bool InflateNodeDirectlyIntoGraph(const INodeController& InNode) = 0;

			/** Creates and inserts a new subgraph into this graph using the given metadata.
			 * By calling AsGraph() on the returned node handle, callers can modify
			 * the new subgraph.
			 *
			 * @param InInfo - Metadata for the subgraph.
			 *
			 * @return Handle to the subgraph node. On error, the handle is invalid.
			 */
			virtual FNodeHandle CreateEmptySubgraph(const FMetasoundFrontendClassMetadata& InInfo) = 0;

			/** Creates a runtime operator for the given graph.
			 *
			 * @param InSettings - Settings to use when creating operators.
			 * @param InEnvironment - Environment variables available during creation.
			 * @param OutBuildErrors - An array to populate with errors encountered during the build process.
			 *
			 * @return On success, a valid pointer to a Metasound operator. An invalid pointer on failure. 
			 */
			virtual TUniquePtr<IOperator> BuildOperator(const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<IOperatorBuilder::FBuildErrorPtr>& OutBuildErrors) const = 0;

			/** Returns a handle to the document owning this graph. */
			virtual FDocumentHandle GetOwningDocument() = 0;

			/** Returns a handle to the document owning this graph. */
			virtual FConstDocumentHandle GetOwningDocument() const = 0;
		};

		/* An IDocumentController provides methods for querying and manipulating a Metasound document. */
		class METASOUNDFRONTEND_API IDocumentController : public TSharedFromThis<IDocumentController>, public IDocumentAccessor
		{
		public:
			static FDocumentHandle GetInvalidHandle();

			/** Create a document from FMetasoundFrontendDocument description pointer. */
			static FDocumentHandle CreateDocumentHandle(FDocumentAccessPtr InDocument);
			/** Create a document from FMetasoundFrontendDocument description pointer. */
			static FConstDocumentHandle CreateDocumentHandle(FConstDocumentAccessPtr InDocument);

			IDocumentController() = default;
			virtual ~IDocumentController() = default;

			/** Returns true if the controller is in a valid state. */
			virtual bool IsValid() const = 0;

			/** Returns an array of inputs which describe the required inputs needed to satisfy the document archetype. */
			virtual const TArray<FMetasoundFrontendClassVertex>& GetRequiredInputs() const = 0;

			/** Returns an array of outputs which describe the required outputs needed to satisfy the document archetype. */
			virtual const TArray<FMetasoundFrontendClassVertex>& GetRequiredOutputs() const = 0;

			// TODO: add info on environment variables. 
			// TODO: consider find/add subgraph
			// TODO: perhaps functions returning access pointers could be removed from main interface and only exist in FDocumentController.
			
			/** Returns an array of all class dependencies for this document. */
			virtual TArray<FMetasoundFrontendClass> GetDependencies() const = 0;
			virtual TArray<FMetasoundFrontendGraphClass> GetSubgraphs() const = 0;
			virtual TArray<FMetasoundFrontendClass> GetClasses() const = 0;

			virtual FConstClassAccessPtr FindDependencyWithID(FGuid InClassID) const = 0;
			virtual FConstGraphClassAccessPtr FindSubgraphWithID(FGuid InClassID) const = 0;
			virtual FConstClassAccessPtr FindClassWithID(FGuid InClassID) const = 0;


			/** Returns an existing Metasound class description corresponding to 
			 * a dependency which matches the provided class information.
			 *
			 * @return A pointer to the found object, or nullptr if it could not be found.
			 */
			virtual FConstClassAccessPtr FindClass(const FNodeClassInfo& InNodeClass) const = 0; // TODO: swap for just using metadata in future. 

			/** Attempts to find an existing Metasound class description corresponding
			 * to a dependency which matches the provided class information. If the
			 * class is not found in the current dependencies, it is added to the 
			 * dependencies.
			 *
			 * @return A pointer to the object, or nullptr on error.
			 */
			virtual FConstClassAccessPtr FindOrAddClass(const FNodeClassInfo& InNodeClass) = 0; // TODO: swap for just using metadata in future. 

			/** Returns an existing Metasound class description corresponding to 
			 * a dependency which matches the provided class information.
			 *
			 * @return A pointer to the found object, or nullptr if it could not be found.
			 */
			virtual FConstClassAccessPtr FindClass(const FMetasoundFrontendClassMetadata& InMetadata) const = 0;

			/** Attempts to find an existing Metasound class description corresponding
			 * to a dependency which matches the provided class information. If the
			 * class is not found in the current dependencies, it is added to the 
			 * dependencies.
			 *
			 * @return A pointer to the object, or nullptr on error.
			 */
			virtual FConstClassAccessPtr FindOrAddClass(const FMetasoundFrontendClassMetadata& InMetadata) = 0;

			/** Removes all dependencies which are no longer referenced by any graphs within this document
			  * and updates dependency Metadata where necessary with that found in the registry.  */
			virtual void SynchronizeDependencies() = 0;

			/** Returns an array of all subgraphs for this document. */
			virtual TArray<FGraphHandle> GetSubgraphHandles() = 0;

			/** Returns an array of all subgraphs for this document. */
			virtual TArray<FConstGraphHandle> GetSubgraphHandles() const = 0;

			/** Returns a graphs in the document with the given class ID.*/
			virtual FGraphHandle GetSubgraphWithClassID(FGuid InClassID) = 0;

			/** Returns a graphs in the document with the given class ID.*/
			virtual FConstGraphHandle GetSubgraphWithClassID(FGuid InClassID) const = 0;

			/** Returns the root graph of this document. */
			virtual FGraphHandle GetRootGraph() = 0;

			/** Returns the root graph of this document. */
			virtual FConstGraphHandle GetRootGraph() const = 0;

			/** Exports the document to a json file at the provided path.
			 *
			 * @return True on success, false on failure. 
			 */
			virtual bool ExportToJSONAsset(const FString& InAbsolutePath) const = 0;

			/** Exports the document to a json formatted string. */
			virtual FString ExportToJSON() const = 0;
		};
	}
}
