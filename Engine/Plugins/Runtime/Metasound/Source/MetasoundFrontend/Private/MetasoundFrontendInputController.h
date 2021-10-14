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
		/** FBaseInputController provides common functionality for multiple derived
		 * input controllers.
		 */
		class FBaseInputController : public IInputController 
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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
			const FVertexName& GetName() const override;

			const FMetasoundFrontendLiteral* GetLiteral() const override;
			void SetLiteral(const FMetasoundFrontendLiteral& InLiteral) override;

			const FMetasoundFrontendLiteral* GetClassDefaultLiteral() const override;

			// This only exists to allow for transform fix-ups to easily cleanup input/output
			// vertex names & should not be used for typical edit or runtime callsites.
			void SetName(const FVertexName& InName) override { checkNoEntry(); }

			// Input metadata
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;

			// Owning node info
			FGuid GetOwningNodeID() const override;
			FNodeHandle GetOwningNode() override;
			FConstNodeHandle GetOwningNode() const override;

			// Connection info
			bool IsConnected() const override;
			FOutputHandle GetConnectedOutput() override;
			FConstOutputHandle GetConnectedOutput() const override;

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
		class FOutputNodeInputController : public FBaseInputController 
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;
			void SetName(const FVertexName& InName) override;

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
		class FInputNodeInputController : public FBaseInputController 
		{
			using FRegistry = FMetasoundFrontendRegistryContainer;
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
			FText GetDisplayName() const override;
			const FText& GetTooltip() const override;
			const FMetasoundFrontendVertexMetadata& GetMetadata() const override;

			void SetName(const FVertexName& InName) override;

			FConnectability CanConnectTo(const IOutputController& InController) const override;
			bool Connect(IOutputController& InController) override;

			// Connection controls.
			bool ConnectWithConverterNode(IOutputController& InController, const FConverterNodeInfo& InNodeClassName) override;

		private:
			FConstClassInputAccessPtr OwningGraphClassInputPtr;
		};
	}
}
