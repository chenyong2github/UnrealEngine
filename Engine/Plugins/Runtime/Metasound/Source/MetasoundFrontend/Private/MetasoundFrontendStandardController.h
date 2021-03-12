// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFrontendBaseClasses.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendInvalidController.h"
#include "MetasoundLog.h"
#include "UObject/Object.h"


namespace Metasound
{
	namespace Frontend
	{
		/** FBaseOutputController provides common functionality for multiple derived
		 * output controllers.
		 */
		class METASOUNDFRONTEND_API FBaseOutputController: public IOutputController
		{
		public:

			struct FInitParams
			{
				FGuid ID;

				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassOutputAccessPtr ClassOutputPtr;
				FGraphAccessPtr GraphPtr; 

				/* Node handle which owns this output. */
				FNodeHandle OwningNode;
			};

			/** Construct the output controller base.  */
			FBaseOutputController(const FInitParams& InParams);

			virtual ~FBaseOutputController() = default;

			bool IsValid() const override;

			FGuid GetID() const override;
			const FName& GetDataType() const override;
			const FString& GetName() const override;

			// Output metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;

			// Return info on containing node. 
			FGuid GetOwningNodeID() const override;
			FNodeHandle GetOwningNode() override;
			FConstNodeHandle GetOwningNode() const override;

			TArray<FInputHandle> GetCurrentlyConnectedInputs() override;
			TArray<FConstInputHandle> GetCurrentlyConnectedInputs() const override;
			bool Disconnect() override;

			// Connection logic.
			FConnectability CanConnectTo(const IInputController& InController) const override;
			bool Connect(IInputController& InController) override;
			bool ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName) override;
			bool Disconnect(IInputController& InController) override;

		protected:
			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;


			FGuid ID;
			FConstVertexAccessPtr NodeVertexPtr;	
			FConstClassOutputAccessPtr ClassOutputPtr;
			FGraphAccessPtr GraphPtr; 
			FNodeHandle OwningNode;

		private:

			TArray<FMetasoundFrontendEdge> FindEdges() const;
		};


		/** FInputNodeOutputController represents the output vertex of an input 
		 * node. 
		 *
		 * FInputNodeOutputController is largely to represent inputs coming into 
		 * graph. 
		 */
		class METASOUNDFRONTEND_API FInputNodeOutputController : public FBaseOutputController
		{
		public:
			struct FInitParams
			{
				FGuid ID;

				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassOutputAccessPtr ClassOutputPtr;
				FConstClassInputAccessPtr OwningGraphClassInputPtr;
				FGraphAccessPtr GraphPtr; 

				/* Node handle which owns this output. */
				FNodeHandle OwningNode;
			};

			/** Constructs the output controller. */
			FInputNodeOutputController(const FInitParams& InParams);

			virtual ~FInputNodeOutputController() = default;

			bool IsValid() const override;

			// Output metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;


		protected:
			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;

		private:
			mutable FText CachedDisplayName;

			FConstClassInputAccessPtr OwningGraphClassInputPtr;
		};

		/** FOutputNodeOutputController represents the output vertex of an input 
		 * node. 
		 *
		 * FOutputNodeOutputController is largely to represent inputs coming into 
		 * graph. 
		 */
		class METASOUNDFRONTEND_API FOutputNodeOutputController : public FBaseOutputController
		{
		public:
			struct FInitParams
			{
				FGuid ID;

				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassOutputAccessPtr ClassOutputPtr;
				FConstClassOutputAccessPtr OwningGraphClassOutputPtr;
				FGraphAccessPtr GraphPtr; 

				/* Node handle which owns this output. */
				FNodeHandle OwningNode;
			};

			/** Constructs the output controller. */
			FOutputNodeOutputController(const FInitParams& InParams);

			virtual ~FOutputNodeOutputController() = default;

			bool IsValid() const override;

			// Output metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;

			FConnectability CanConnectTo(const IInputController& InController) const override;
			bool Connect(IInputController& InController) override;
			bool ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName) override;

		private:
			FConstClassOutputAccessPtr OwningGraphClassOutputPtr;
		};

		
		/** FBaseInputController provides common functionality for multiple derived
		 * input controllers.
		 */
		class METASOUNDFRONTEND_API FBaseInputController : public IInputController 
		{
		public:

			struct FInitParams
			{
				FGuid ID; 
				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassInputAccessPtr ClassInputPtr;
				FGraphAccessPtr GraphPtr; 
				FNodeHandle OwningNode;
			};

			/** Construct the input controller base. */
			FBaseInputController(const FInitParams& InParams);

			virtual ~FBaseInputController() = default;

			bool IsValid() const override;

			FGuid GetID() const override;
			const FName& GetDataType() const override;
			const FString& GetName() const override;

			const FMetasoundFrontendLiteral* GetDefaultLiteral() const override;

			// Input metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;

			// Owning node info
			FGuid GetOwningNodeID() const override;
			FNodeHandle GetOwningNode() override;
			FConstNodeHandle GetOwningNode() const override;

			// Connection info
			bool IsConnected() const override;
			FOutputHandle GetCurrentlyConnectedOutput() override;
			FConstOutputHandle GetCurrentlyConnectedOutput() const override;

			FConnectability CanConnectTo(const IOutputController& InController) const override;
			bool Connect(IOutputController& InController) override;

			// Connection controls.
			bool ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName) override;

			bool Disconnect(IOutputController& InController) override;
			bool Disconnect() override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;

			const FMetasoundFrontendEdge* FindEdge() const;
			FMetasoundFrontendEdge* FindEdge();

			FGuid ID;
			FConstVertexAccessPtr NodeVertexPtr;
			FConstClassInputAccessPtr ClassInputPtr;
			FGraphAccessPtr GraphPtr;
			FNodeHandle OwningNode;


		};

		/** FOutputNodeInputController represents the input vertex of an output 
		 * node. 
		 *
		 * FOutputNodeInputController is largely to represent outputs exposed from
		 * a graph. 
		 */
		class METASOUNDFRONTEND_API FOutputNodeInputController : public FBaseInputController 
		{
		public:
			struct FInitParams
			{
				FGuid ID; 
				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassInputAccessPtr ClassInputPtr;
				FConstClassOutputAccessPtr OwningGraphClassOutputPtr;
				FGraphAccessPtr GraphPtr; 
				FNodeHandle OwningNode;
			};

			/** Constructs the input controller. */
			FOutputNodeInputController(const FInitParams& InParams);

			bool IsValid() const override;

			// Input metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendLiteral* GetDefaultLiteral() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;

		private:

			mutable FText CachedDisplayName;

			FConstClassOutputAccessPtr OwningGraphClassOutputPtr;
		};

		/** FInputNodeInputController represents the input vertex of an output 
		 * node. 
		 *
		 * FInputNodeInputController is largely to represent outputs exposed from
		 * a graph. 
		 */
		class METASOUNDFRONTEND_API FInputNodeInputController : public FBaseInputController 
		{
		public:
			struct FInitParams
			{
				FGuid ID; 
				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassInputAccessPtr ClassInputPtr;
				FConstClassInputAccessPtr OwningGraphClassInputPtr;
				FGraphAccessPtr GraphPtr; 
				FNodeHandle OwningNode;
			};

			/** Constructs the input controller. */
			FInputNodeInputController(const FInitParams& InParams);

			bool IsValid() const override;

			// Input metadata
			const FText& GetDisplayName() const override;
			const FText& GetTooltip() const override;

			FConnectability CanConnectTo(const IOutputController& InController) const override;
			bool Connect(IOutputController& InController) override;

			// Connection controls.
			bool ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName) override;


		private:

			FConstClassInputAccessPtr OwningGraphClassInputPtr;
		};

		/** FBaseNodeController provides common functionality for multiple derived
		 * node controllers.
		 */
		class METASOUNDFRONTEND_API FBaseNodeController : public INodeController
		{
		public:

			struct FInitParams
			{
				FNodeAccessPtr NodePtr;
				FConstClassAccessPtr ClassPtr;
				FGraphHandle OwningGraph;
			};

			/** Construct a base node controller. */
			FBaseNodeController(const FInitParams& InParams);

			bool IsValid() const override;

			// Owning graph info
			FGuid GetOwningGraphClassID() const override;
			FGraphHandle GetOwningGraph() override;
			FConstGraphHandle GetOwningGraph() const override;

			// Info about this node.
			FGuid GetID() const override;
			FGuid GetClassID() const override;
			const FText& GetClassDisplayName() const override;

			FMetasoundFrontendVersionNumber GetClassVersionNumber() const override;
			const FText& GetClassDescription() const override;
			const FMetasoundFrontendInterfaceStyle& GetInputStyle() const override;
			const FMetasoundFrontendInterfaceStyle& GetOutputStyle() const override;
			const FMetasoundFrontendClassStyle& GetClassStyle() const override;

			const FMetasoundFrontendNodeStyle& GetNodeStyle() const override;
			void SetNodeStyle(const FMetasoundFrontendNodeStyle& InStyle) override;

			const FString& GetNodeName() const override;
			EMetasoundFrontendClassType GetClassType() const override;

			bool CanAddInput(const FString& InVertexName) const override;
			FInputHandle AddInput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) override;
			bool RemoveInput(FGuid InVertexID) override;

			bool CanAddOutput(const FString& InVertexName) const override;
			FInputHandle AddOutput(const FString& InVertexName, const FMetasoundFrontendLiteral* InDefault) override;
			bool RemoveOutput(FGuid InVertexID) override;

			const FMetasoundFrontendClassName& GetClassName() const override;

			/** Returns all node inputs. */
			TArray<FInputHandle> GetInputs() override;

			/** Returns all node inputs. */
			TArray<FConstInputHandle> GetConstInputs() const override;

			TArray<FInputHandle> GetInputsWithVertexName(const FString& InName) override;
			TArray<FConstInputHandle> GetInputsWithVertexName(const FString& InName) const override;

			/** Returns all node outputs. */
			TArray<FOutputHandle> GetOutputs() override;

			/** Returns all node outputs. */
			TArray<FConstOutputHandle> GetConstOutputs() const override;

			TArray<FOutputHandle> GetOutputsWithVertexName(const FString& InName) override;
			TArray<FConstOutputHandle> GetOutputsWithVertexName(const FString& InName) const override;

			/** Returns an input with the given id. 
			 *
			 * If the input does not exist, an invalid handle is returned. 
			 */
			FInputHandle GetInputWithID(FGuid InVertexID) override;

			/** Returns an input with the given name. 
			 *
			 * If the input does not exist, an invalid handle is returned. 
			 */
			FConstInputHandle GetInputWithID(FGuid InVertexID) const override;

			/** Returns an output with the given name. 
			 *
			 * If the output does not exist, an invalid handle is returned. 
			 */
			FOutputHandle GetOutputWithID(FGuid InVertexID) override;

			/** Returns an output with the given name. 
			 *
			 * If the output does not exist, an invalid handle is returned. 
			 */
			FConstOutputHandle GetOutputWithID(FGuid InVertexID) const override;

			FGraphHandle AsGraph() override;
			FConstGraphHandle AsGraph() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;

			FNodeAccessPtr NodePtr;
			FConstClassAccessPtr ClassPtr;
			FGraphHandle OwningGraph;

		private:

			struct FInputControllerParams
			{
				FGuid VertexID;
				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassInputAccessPtr ClassInputPtr;
			};

			struct FOutputControllerParams
			{
				FGuid VertexID;
				FConstVertexAccessPtr NodeVertexPtr;
				FConstClassOutputAccessPtr ClassOutputPtr;
			};

			TArray<FInputControllerParams> GetInputControllerParams() const;
			TArray<FOutputControllerParams> GetOutputControllerParams() const;

			TArray<FInputControllerParams> GetInputControllerParamsWithVertexName(const FString& InName) const;
			TArray<FOutputControllerParams> GetOutputControllerParamsWithVertexName(const FString& InName) const;

			bool FindInputControllerParamsWithID(FGuid InVertexID, FInputControllerParams& OutParams) const;
			bool FindOutputControllerParamsWithID(FGuid InVertexID, FOutputControllerParams& OutParams) const;

			const FMetasoundFrontendClassInput* FindClassInputWithName(const FString& InName) const;
			const FMetasoundFrontendClassOutput* FindClassOutputWithName(const FString& InName) const;

			const FMetasoundFrontendVertex* FindNodeInputWithName(const FString& InName) const;
			const FMetasoundFrontendVertex* FindNodeOutputWithName(const FString& InName) const;



			virtual FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const = 0;

			virtual FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const = 0;
		};

		/** FNodeController represents a external or subgraph node. */
		class METASOUNDFRONTEND_API FNodeController: public FBaseNodeController
		{
			// Private token only allows members or friends to call constructor.
			enum EPrivateToken { Token };

		public:
			struct FInitParams
			{
				FNodeAccessPtr NodePtr;
				FConstClassAccessPtr ClassPtr;
				FGraphAccessPtr GraphPtr;
				FGraphHandle OwningGraph;
			};

			// Constructor takes a private token so it can only be instantiated by
			// using the static creation functions. This protects against some
			// error conditions which would result in a zombie object. The creation
			// methods can detect the error conditions and return an invalid controller
			// on error
			FNodeController(EPrivateToken InToken, const FInitParams& InParams);

			/** Create a node handle for a external or subgraph node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FNodeHandle CreateNodeHandle(const FInitParams& InParams);

			/** Create a node handle for a external or subgraph node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FConstNodeHandle CreateConstNodeHandle(const FInitParams& InParams);

			virtual ~FNodeController() = default;

			bool IsValid() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;


		private:

			FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const override;

			FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const override;

			FGraphAccessPtr GraphPtr;

		};

		/** FOutputNodeController represents an output node. */
		class METASOUNDFRONTEND_API FOutputNodeController: public FBaseNodeController
		{
			// Private token only allows members or friends to call constructor.
			enum EPrivateToken { Token };

		public:
			struct FInitParams
			{
				FNodeAccessPtr NodePtr;
				FConstClassAccessPtr ClassPtr;
				FConstClassOutputAccessPtr OwningGraphClassOutputPtr; 
				FGraphAccessPtr GraphPtr;
				FGraphHandle OwningGraph;
			};

			// Constructor takes a private token so it can only be instantiated by
			// using the static creation functions. This protects against some
			// error conditions which would result in a zombie object. The creation
			// methods can detect the error conditions and return an invalid controller
			// on error
			FOutputNodeController(EPrivateToken InToken, const FInitParams& InParams);

			/** Create a node handle for an output node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FNodeHandle CreateOutputNodeHandle(const FInitParams& InParams);

			/** Create a node handle for an output node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FConstNodeHandle CreateConstOutputNodeHandle(const FInitParams& InParams);

			virtual ~FOutputNodeController() = default;

			bool IsValid() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;


		private:
			FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const override;

			FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const override;

			FGraphAccessPtr GraphPtr;
			FConstClassOutputAccessPtr OwningGraphClassOutputPtr; 
		};

		/** FInputNodeController represents an input node. */
		class METASOUNDFRONTEND_API FInputNodeController: public FBaseNodeController
		{
			// Private token only allows members or friends to call constructor.
			enum EPrivateToken { Token };

		public:
			struct FInitParams
			{
				FNodeAccessPtr NodePtr;
				FConstClassAccessPtr ClassPtr;
				FConstClassInputAccessPtr OwningGraphClassInputPtr; 
				FGraphAccessPtr GraphPtr;
				FGraphHandle OwningGraph;
			};

			// Constructor takes a private token so it can only be instantiated by
			// using the static creation functions. This protects against some
			// error conditions which would result in a zombie object. The creation
			// methods can detect the error conditions and return an invalid controller
			// on error
			FInputNodeController(EPrivateToken InToken, const FInitParams& InParams);

			/** Create a node handle for an input node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FNodeHandle CreateInputNodeHandle(const FInitParams& InParams);

			/** Create a node handle for an input node. 
			 *
			 * @return A Node handle. On error, an invalid node handle is returned. 
			 */
			static FConstNodeHandle CreateConstInputNodeHandle(const FInitParams& InParams);

			virtual ~FInputNodeController() = default;

			bool IsValid() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;



		private:
			FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const override;

			FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const override;

			FConstClassInputAccessPtr OwningGraphClassInputPtr;
			FGraphAccessPtr GraphPtr;
		};

		/** FGraphController represents a Metasound graph class. */
		class METASOUNDFRONTEND_API FGraphController : public IGraphController
		{

			// Private token only allows members or friends to call constructor.
			enum EPrivateToken { Token };

		public:

			struct FInitParams
			{
				FGraphClassAccessPtr GraphClassPtr;
				FDocumentHandle OwningDocument;
			};

			// Constructor takes a private token so it can only be instantiated by
			// using the static creation functions. This protects against some
			// error conditions which would result in a zombie object. The creation
			// methods can detect the error conditions and return an invalid controller
			// on error
			FGraphController(EPrivateToken InToken, const FInitParams& InParams);

			/** Create a graph handle. 
			 *
			 * @return A graph handle. On error, an invalid handle is returned. 
			 */
			static FGraphHandle CreateGraphHandle(const FInitParams& InParams);

			/** Create a graph handle. 
			 *
			 * @return A graph handle. On error, an invalid handle is returned. 
			 */
			static FConstGraphHandle CreateConstGraphHandle(const FInitParams& InParams);

			virtual ~FGraphController() = default;

			bool IsValid() const override;

			FGuid GetClassID() const override;

			FGuid GetNewVertexID() const override;

			TArray<FString> GetInputVertexNames() const override;
			TArray<FString> GetOutputVertexNames() const override;
			FConstClassInputAccessPtr FindClassInputWithName(const FString& InName) const override;
			FConstClassOutputAccessPtr FindClassOutputWithName(const FString& InName) const override;
			FGuid GetVertexIDForInputVertex(const FString& InInputName) const override;
			FGuid GetVertexIDForOutputVertex(const FString& InOutputName) const override;

			TArray<FNodeHandle> GetNodes() override;
			TArray<FConstNodeHandle> GetConstNodes() const override;

			FConstNodeHandle GetNodeWithID(FGuid InNodeID) const override;
			FNodeHandle GetNodeWithID(FGuid InNodeID) override;

			TArray<FNodeHandle> GetInputNodes() override;
			TArray<FConstNodeHandle> GetConstInputNodes() const override;

			TArray<FNodeHandle> GetOutputNodes() override;
			TArray<FConstNodeHandle> GetConstOutputNodes() const override;

			bool ContainsInputVertexWithName(const FString& InName) const override;
			bool ContainsOutputVertexWithName(const FString& InName) const override;

			FConstNodeHandle GetInputNodeWithName(const FString& InName) const override;
			FConstNodeHandle GetOutputNodeWithName(const FString& InName) const override;

			FNodeHandle GetInputNodeWithName(const FString& InName) override;
			FNodeHandle GetOutputNodeWithName(const FString& InName) override;

			FNodeHandle AddInputVertex(const FMetasoundFrontendClassInput& InDescription) override;
			bool RemoveInputVertex(const FString& InName) override;

			FNodeHandle AddOutputVertex(const FMetasoundFrontendClassOutput& InDescription) override;
			bool RemoveOutputVertex(const FString& InName) override;

			// This can be used to determine what kind of property editor we should use for the data type of a given input.
			// Will return Invalid if the input couldn't be found, or if the input doesn't support any kind of literals.
			ELiteralType GetPreferredLiteralTypeForInputVertex(const FString& InInputName) const override;

			// For inputs whose preferred literal type is UObject or UObjectArray, this can be used to determine the UObject corresponding to that input's datatype.
			UClass* GetSupportedClassForInputVertex(const FString& InInputName) override;

			FMetasoundFrontendLiteral GetDefaultInput(const FGuid& InVertexID) const override;

			// These can be used to set the default value for a given input on this graph.
			// @returns false if the input name couldn't be found, or if the literal type was incompatible with the Data Type of this input.
			bool SetDefaultInput(const FGuid& InVertexID, const FMetasoundFrontendLiteral& InLiteral) override;
			bool SetDefaultInputToDefaultLiteralOfType(const FGuid& InVertexID) override;

			// Get the description for the input with the given name.
			const FText& GetInputDescription(const FString& InName) const override;

			// Get the description for the output with the given name.
			const FText& GetOutputDescription(const FString& InName) const override;

			// Set the description for the input with the given name
			void SetInputDescription(const FString& InName, const FText& InDescription) override;

			// Set the description for the input with the given name
			void SetOutputDescription(const FString& InName, const FText& InDescription) override;

			// Set the display name for the input with the given name
			void SetInputDisplayName(const FString& InName, const FText& InDisplayName) override;

			// Set the display name for the output with the given name
			void SetOutputDisplayName(const FString& InName, const FText& InDisplayName) override;

			// This can be used to clear the current literal for a given input.
			// @returns false if the input name couldn't be found.
			bool ClearLiteralForInput(const FString& InInputName, FGuid InVertexID) override;

			FNodeHandle AddNode(const FNodeClassInfo& InNodeClass) override;
			FNodeHandle AddNode(const FNodeRegistryKey& InNodeClass) override;
			FNodeHandle AddNode(const FMetasoundFrontendClassMetadata& InClassMetadata) override;

			// Remove the node corresponding to this node handle.
			// On success, invalidates the received node handle.
			bool RemoveNode(INodeController& InNode) override;

			// Returns the metadata for the current graph, including the name, description and author.
			const FMetasoundFrontendClassMetadata& GetGraphMetadata() const override;

			bool InflateNodeDirectlyIntoGraph(const INodeController& InNode) override;

			FNodeHandle CreateEmptySubgraph(const FMetasoundFrontendClassMetadata& InInfo) override;

			TUniquePtr<IOperator> BuildOperator(const FOperatorSettings& InSettings, const FMetasoundEnvironment& InEnvironment, TArray<IOperatorBuilder::FBuildErrorPtr>& OutBuildErrors) const override;

			FDocumentHandle GetOwningDocument() override;
			FConstDocumentHandle GetOwningDocument() const override;

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;

		private:

			// Add/remove nodes
			FNodeHandle AddNode(FConstClassAccessPtr InExistingDependency);
			bool RemoveNode(const FMetasoundFrontendNode& InDesc);

			// Remove inputs
			bool RemoveInput(const FMetasoundFrontendNode& InNode);
			bool RemoveOutput(const FMetasoundFrontendNode& InNode);

			FGuid NewNodeID() const;

			FNodeHandle GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate);
			FConstNodeHandle GetNodeByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const;

			TArray<FNodeHandle> GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc);
			TArray<FConstNodeHandle> GetNodesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InFilterFunc) const;


			struct FNodeAndClass
			{
				FNodeAccessPtr Node;
				FConstClassAccessPtr Class;

				bool IsValid() const { return Node.IsValid() && Class.IsValid(); }
			};

			struct FConstNodeAndClass
			{
				FConstNodeAccessPtr Node;
				FConstClassAccessPtr Class;

				bool IsValid() const { return Node.IsValid() && Class.IsValid(); }
			};

			bool ContainsNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const;
			TArray<FNodeAndClass> GetNodesAndClasses();
			TArray<FConstNodeAndClass> GetNodesAndClasses() const;

			TArray<FNodeAndClass> GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate);
			TArray<FConstNodeAndClass> GetNodesAndClassesByPredicate(TFunctionRef<bool (const FMetasoundFrontendClass&, const FMetasoundFrontendNode&)> InPredicate) const;


			TArray<FNodeHandle> GetNodeHandles(TArrayView<const FNodeAndClass> InNodesAndClasses);
			TArray<FConstNodeHandle> GetNodeHandles(TArrayView<const FConstNodeAndClass> InNodesAndClasses) const;

			FNodeHandle GetNodeHandle(const FNodeAndClass& InNodeAndClass);
			FConstNodeHandle GetNodeHandle(const FConstNodeAndClass& InNodeAndClass) const;

			FMetasoundFrontendClassInput* FindInputDescriptionWithName(const FString& InName);
			const FMetasoundFrontendClassInput* FindInputDescriptionWithName(const FString& InName) const;

			FMetasoundFrontendClassInput* FindInputDescriptionWithVertexID(const FGuid& InVertexID);
			const FMetasoundFrontendClassInput* FindInputDescriptionWithVertexID(const FGuid& InVertexID) const;

			FMetasoundFrontendClassOutput* FindOutputDescriptionWithName(const FString& InName);
			const FMetasoundFrontendClassOutput* FindOutputDescriptionWithName(const FString& InName) const;

			FMetasoundFrontendClassOutput* FindOutputDescriptionWithVertexID(const FGuid& InVertexID);
			const FMetasoundFrontendClassOutput* FindOutputDescriptionWithVertexID(const FGuid& InVertexID) const;

			FClassInputAccessPtr FindInputDescriptionWithNodeID(FGuid InNodeID);
			FConstClassInputAccessPtr FindInputDescriptionWithNodeID(FGuid InNodeID) const;

			FClassOutputAccessPtr FindOutputDescriptionWithNodeID(FGuid InNodeID);
			FConstClassOutputAccessPtr FindOutputDescriptionWithNodeID(FGuid InNodeID) const;

			FMetasoundFrontendClassMetadata CreateInputClassMetadata(const FMetasoundFrontendClassInput& InClassInput);
			FMetasoundFrontendClassMetadata CreateOutputClassMetadata(const FMetasoundFrontendClassOutput& InClassOutput);

			FGraphClassAccessPtr GraphClassPtr;
			FDocumentHandle OwningDocument; 
		};

		/** FDocumentController represents an entire Metasound document. */
		class METASOUNDFRONTEND_API FDocumentController : public IDocumentController 
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;

		public:
			/** Construct a FDocumentController.
			 *
			 * @param InDocument - Document to be manipulated.
			 */
			FDocumentController(FDocumentAccessPtr InDocumentPtr);

			/** Create a FDocumentController.
			 *
			 * @param InDocument - Document to be manipulated.
			 *
			 * @return A document handle. 
			 */
			static FDocumentHandle CreateDocumentHandle(FDocumentAccessPtr InDocument)
			{
				return MakeShared<FDocumentController>(InDocument);
			}

			virtual ~FDocumentController() = default;

			bool IsValid() const override;

			bool IsRequiredInput(const FString& InInputName) const override;
			bool IsRequiredOutput(const FString& InOutputName) const override;

			const TArray<FMetasoundFrontendClassVertex>& GetRequiredInputs() const override;
			const TArray<FMetasoundFrontendClassVertex>& GetRequiredOutputs() const override;

			TArray<FMetasoundFrontendClass> GetDependencies() const override;
			TArray<FMetasoundFrontendGraphClass> GetSubgraphs() const override;
			TArray<FMetasoundFrontendClass> GetClasses() const override;

			FConstClassAccessPtr FindDependencyWithID(FGuid InClassID) const override;
			FConstGraphClassAccessPtr FindSubgraphWithID(FGuid InClassID) const override;
			FConstClassAccessPtr FindClassWithID(FGuid InClassID) const override;

			FConstClassAccessPtr FindClass(const FNodeClassInfo& InNodeClass) const override;
			FConstClassAccessPtr FindClass(const FMetasoundFrontendClassMetadata& InMetadata) const override;

			FConstClassAccessPtr FindOrAddClass(const FNodeClassInfo& InNodeClass) override;
			FConstClassAccessPtr FindOrAddClass(const FMetasoundFrontendClassMetadata& InMetadata) override;

			void RemoveUnreferencedDependencies() override;

			FGraphHandle GetRootGraph() override;
			FConstGraphHandle GetRootGraph() const override;

			/** Returns an array of all subgraphs for this document. */
			TArray<FGraphHandle> GetSubgraphHandles() override;

			/** Returns an array of all subgraphs for this document. */
			TArray<FConstGraphHandle> GetSubgraphHandles() const override;

			/** Returns a graphs in the document with the given class ID.*/
			FGraphHandle GetSubgraphWithClassID(FGuid InClassID) override;

			/** Returns a graphs in the document with the given class ID.*/
			FConstGraphHandle GetSubgraphWithClassID(FGuid InClassID) const override;

			bool ExportToJSONAsset(const FString& InAbsolutePath) const override;
			FString ExportToJSON() const override;

			static bool IsMatchingMetasoundClass(const FMetasoundFrontendClassMetadata& InMetadataA, const FMetasoundFrontendClassMetadata& InMetadataB);
			static bool IsMatchingMetasoundClass(const FNodeClassInfo& InNodeClass, const FMetasoundFrontendClassMetadata& InMetadata);

		protected:
			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;
		private:

			FGuid NewClassID() const;

			FDocumentAccessPtr DocumentPtr;
		};
	}
}


