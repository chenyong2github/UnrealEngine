// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "Misc/Guid.h"

namespace Metasound
{
	namespace Frontend
	{
		/** FBaseNodeController provides common functionality for multiple derived
		 * node controllers.
		 */
		class FBaseNodeController : public INodeController
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

			bool ClearInputLiteral(FGuid InVertexID) override;
			const FMetasoundFrontendLiteral* GetInputLiteral(const FGuid& InVertexID) const override;
			void SetInputLiteral(const FMetasoundFrontendVertexLiteral& InVertexLiteral) override;

			const FMetasoundFrontendClassInterface& GetClassInterface() const override;
			const FMetasoundFrontendClassMetadata& GetClassMetadata() const override;
			const FMetasoundFrontendInterfaceStyle& GetInputStyle() const override;
			const FMetasoundFrontendInterfaceStyle& GetOutputStyle() const override;
			const FMetasoundFrontendClassStyle& GetClassStyle() const override;

			/** Description of the given node. */
			const FText& GetDescription() const override;

			const FMetasoundFrontendNodeStyle& GetNodeStyle() const override;
			void SetNodeStyle(const FMetasoundFrontendNodeStyle& InStyle) override;

			bool DiffAgainstRegistryInterface(FClassInterfaceUpdates& OutInterfaceUpdates, bool bInUseHighestMinorVersion) const override;

			bool CanAutoUpdate(FClassInterfaceUpdates* OutInterfaceUpdates = nullptr) const override;
			FNodeHandle ReplaceWithVersion(const FMetasoundFrontendVersionNumber& InNewVersion) override;
			FMetasoundFrontendVersionNumber FindHighestVersionInRegistry() const override;
			FMetasoundFrontendVersionNumber FindHighestMinorVersionInRegistry() const override;

			const FVertexName& GetNodeName() const override;

			// This only exists to allow for transform fix-ups to easily cleanup input/output node names.
			void SetNodeName(const FVertexName& InName) override { checkNoEntry(); }

			/** Returns the readable display name of the given node (Used only within MetaSound
			  * Editor context, and not guaranteed to be a unique identifier). */
			FText GetDisplayName() const override;

			/** Sets the description of the node. */
			void SetDescription(const FText& InDescription) override { }

			/** Sets the display name of the node. */
			void SetDisplayName(const FText& InDisplayName) override { };

			/** Returns the title of the given node (what to label in visual node). */
			const FText& GetDisplayTitle() const override;

			bool CanAddInput(const FVertexName& InVertexName) const override;
			FInputHandle AddInput(const FVertexName& InVertexName, const FMetasoundFrontendLiteral* InDefault) override;
			bool RemoveInput(FGuid InVertexID) override;

			bool CanAddOutput(const FVertexName& InVertexName) const override;
			FInputHandle AddOutput(const FVertexName& InVertexName, const FMetasoundFrontendLiteral* InDefault) override;
			bool RemoveOutput(FGuid InVertexID) override;

			/** Returns all node inputs. */
			TArray<FInputHandle> GetInputs() override;

			/** Returns all node inputs. */
			TArray<FConstInputHandle> GetConstInputs() const override;

			void IterateConstInputs(TUniqueFunction<void(FConstInputHandle)> InFunction) const override;
			void IterateConstOutputs(TUniqueFunction<void(FConstOutputHandle)> InFunction) const override;

			void IterateInputs(TUniqueFunction<void(FInputHandle)> InFunction) override;
			void IterateOutputs(TUniqueFunction<void(FOutputHandle)> InFunction) override;

			int32 GetNumInputs() const override;
			int32 GetNumOutputs() const override;

			FInputHandle GetInputWithVertexName(const FVertexName& InName) override;
			FConstInputHandle GetConstInputWithVertexName(const FVertexName& InName) const override;

			/** Returns all node outputs. */
			TArray<FOutputHandle> GetOutputs() override;

			/** Returns all node outputs. */
			TArray<FConstOutputHandle> GetConstOutputs() const override;

			FOutputHandle GetOutputWithVertexName(const FVertexName& InName) override;
			FConstOutputHandle GetConstOutputWithVertexName(const FVertexName& InName) const override;

			bool IsRequired() const override;

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

			virtual TArray<FInputControllerParams> GetInputControllerParams() const;
			virtual TArray<FOutputControllerParams> GetOutputControllerParams() const;

			virtual bool FindInputControllerParamsWithVertexName(const FVertexName& InName, FInputControllerParams& OutParams) const;
			virtual bool FindOutputControllerParamsWithVertexName(const FVertexName& InName, FOutputControllerParams& OutParams) const;

			virtual bool FindInputControllerParamsWithID(FGuid InVertexID, FInputControllerParams& OutParams) const;
			virtual bool FindOutputControllerParamsWithID(FGuid InVertexID, FOutputControllerParams& OutParams) const;

		private:

			virtual FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const = 0;
			virtual FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const = 0;
		};

		/** Represents an external node (defined in either code or by an asset's root graph). */
		class FNodeController : public FBaseNodeController
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
		class FOutputNodeController: public FBaseNodeController
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
			const FText& GetDescription() const override;
			FText GetDisplayName() const override;
			const FText& GetDisplayTitle() const override;
			void SetDescription(const FText& InDescription) override;
			void SetDisplayName(const FText& InText) override;
			void SetNodeName(const FVertexName& InName) override;
			bool IsRequired() const override;

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
		class FInputNodeController: public FBaseNodeController
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

			const FText& GetDescription() const override;
			FText GetDisplayName() const override;
			const FText& GetDisplayTitle() const override;
			bool IsRequired() const override;
			bool IsValid() const override;
			void SetDescription(const FText& InDescription) override;
			void SetDisplayName(const FText& InText) override;
			void SetNodeName(const FVertexName& InName) override;

			// No-ops as inputs do not handle literals the same way as other nodes
			bool ClearInputLiteral(FGuid InVertexID) override { return false; }
			const FMetasoundFrontendLiteral* GetInputLiteral(const FGuid& InVertexID) const override { return nullptr; }
			void SetInputLiteral(const FMetasoundFrontendVertexLiteral& InVertexLiteral) override { }

		protected:

			FDocumentAccess ShareAccess() override;
			FConstDocumentAccess ShareAccess() const override;


		private:
			FInputHandle CreateInputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassInputAccessPtr InClassInputPtr, FNodeHandle InOwningNode) const override;

			FOutputHandle CreateOutputController(FGuid InVertexID, FConstVertexAccessPtr InNodeVertexPtr, FConstClassOutputAccessPtr InClassOutputPtr, FNodeHandle InOwningNode) const override;

			FConstClassInputAccessPtr OwningGraphClassInputPtr;
			FGraphAccessPtr GraphPtr;
		};
	}
}

