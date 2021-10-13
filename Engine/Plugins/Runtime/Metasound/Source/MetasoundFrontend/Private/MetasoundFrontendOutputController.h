// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentAccessPtr.h"
#include "MetasoundVertex.h"
#include "UObject/Object.h"

namespace Metasound
{
	namespace Frontend
	{
		/** FBaseOutputController provides common functionality for multiple derived
		 * output controllers.
		 */
		class FBaseOutputController : public IOutputController
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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
			const FVertexName& GetName() const override;

			// Output metadata
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;

			// Return info on containing node.
			FGuid GetOwningNodeID() const override;
			FNodeHandle GetOwningNode() override;
			FConstNodeHandle GetOwningNode() const override;

			void SetName(const FVertexName& InName) override { }

			bool IsConnected() const override;
			TArray<FInputHandle> GetConnectedInputs() override;
			TArray<FConstInputHandle> GetConstConnectedInputs() const override;
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
		class FInputNodeOutputController : public FBaseOutputController
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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

			// Input metadata
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;

			void SetName(const FVertexName& InName) override;

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
		class FOutputNodeOutputController : public FBaseOutputController
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;

			void SetName(const FVertexName& InName) override;

			FConnectability CanConnectTo(const IInputController& InController) const override;
			bool Connect(IInputController& InController) override;
			bool ConnectWithConverterNode(IInputController& InController, const FConverterNodeInfo& InNodeClassName) override;

		private:
			FConstClassOutputAccessPtr OwningGraphClassOutputPtr;
		};
	} // namespace frontend
} // namespace metasound

