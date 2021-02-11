// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/TVariant.h"
#include "MetasoundAccessPtr.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"

namespace Metasound
{
	namespace Frontend
	{
		// This is called by FMetasoundFrontendModule, and flushes any node or datatype registration that was done prior to boot.
		void InitializeFrontend();

		/** Use this to get a reference to the static registry of all nodes implemented in C++. */
		METASOUNDFRONTEND_API TMap<FNodeRegistryKey, FNodeRegistryElement>& GetExternalNodeRegistry();

		// Convenience functions to create an INode corresponding to a specific input or output for a metasound graph.
		// @returns nullptr if the type given wasn't found.
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructInputNode(const FName& InInputType, FInputNodeConstructorParams&& InParams);
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructOutputNode(const FName& InOutputType, const FOutputNodeConstructorParams& InParams);

		// Convenience functions to create an INodeB corresponding to a specific externally declared node type.
		// InNodeType and InNodeHash can be retrieved from the FNodeClassInfo generated from the node registry queries in the metasound frontend (GetAllAvailableNodeClasses, GetAllNodeClassesInNamespace, etc.)
		// @returns nullptr if the type given wasn't found.
		METASOUNDFRONTEND_API TUniquePtr<INode> ConstructExternalNode(const FName& InNodeClassFullName, uint32 InNodeHash, const FNodeInitData& InInitData);

		// Utility base class for classes that want to support naive multi-level undo/redo.
		class METASOUNDFRONTEND_API ITransactable : public TSharedFromThis<ITransactable>
		{
		public:

			ITransactable() = delete;
			ITransactable(uint32 InUndoLimit, TWeakObjectPtr<UObject> InOwningAsset = nullptr);

			/*
			 * Undo a single operation for this or any of it's owned transactables.
			 * @returns true on success.
			 */
			bool Undo();

			/*
			 * Redo a single operation for this or any of it's owned transactables.
			 * @returns true on success.
			 */
			bool Redo();

		protected:
			struct FReversibleTransaction
			{
				// Function that will be executed when Undo is called.
				// This should return true on success.
				TFunction<bool()> UndoTransaction;

				// Function that will be executed when Redo is called.
				// This should return true on success.
				TFunction<bool()> RedoTransaction;

				FReversibleTransaction(TFunction<bool()>&& InUndo, TFunction<bool()>&& InRedo)
					: UndoTransaction(MoveTemp(InUndo))
					, RedoTransaction(MoveTemp(InRedo))
				{
				}
			};

			// Commits an undoable/redoable transaction for this object.
			void CommitTransaction(FReversibleTransaction&& InTransactionDescription);
			
			// Use this function to denote an ITransactable that should consider this ITransactable part of it's undo roll.
			// For example, if an undoable transaction is committed to a FNodeHandle,
			// and you want FGraphHandle::Undo to be able to undo that transaction,
			// you would call FInputHandle::RegisterOwningTransactable(OwningGraphHandle). 
			// 
			// @returns true on success, false if registering this as an owning transaction would create a cycle.
			bool RegisterOwningTransactable(ITransactable& InOwningTransactable);

		private:

			// Execute the topmost transaction on the undo stack,
			// push it to redo stack,
			// and notify the OnwingTransactable.
			bool PerformLocalUndo();

			// Execute the topmost transaction on the redo stack,
			// push it to the undo stack,
			// and notify the OwningTransactable.
			bool PerformLocalRedo();

			// Called from a owned transactable to denote that it is popping an undo or redo action.
			// When this occurs, we discard the topmost call to the owned transactable from the UndoTransactableStack.
			bool DiscardUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);
			bool DiscardRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);

			bool PushUndoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);
			bool PushRedoFromOwnedTransactable(TWeakPtr<ITransactable> InOwnedTransactable);

			// TODO: Make these ring buffers to avoid constantly moving elements around.
			TArray<TWeakPtr<ITransactable>> UndoTransactableStack;
			TArray<TWeakPtr<ITransactable>> RedoTransactableStack;
			TArray<FReversibleTransaction> LocalUndoTransactionStack;
			TArray<FReversibleTransaction> LocalRedoTransactionStack;

			// When CommitTransaction is called,
			// this ITransactable is pushed to the ITransactable::TransactableStack of this ITransactable object.
			TWeakPtr<ITransactable> OwningTransactable;

		protected:
			uint32 UndoLimit;

			// If this transactable is operating on a UAsset, this can be filled to mark it's corresponding package as dirty.
			TWeakObjectPtr<UObject> OwningAsset;
		};
	}
}
