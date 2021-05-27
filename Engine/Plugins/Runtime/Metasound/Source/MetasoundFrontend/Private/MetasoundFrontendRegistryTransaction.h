// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"

namespace Metasound
{
	namespace Frontend
	{
		using FRegistryTransactionID = int32;

		/** Returns an ID representing the beginning of the transaction history. */
		FRegistryTransactionID GetOriginRegistryTransactionID();

		/** Describes the type of transaction. */
		enum class ETransactionType : uint8
		{
			Add,     //< Something was added to the registry.
			Remove   //< Something was removed from the registry.
		};

		/** Interface for a registry transaction. */
		class IRegistryTransaction
		{
		public:
			virtual ~IRegistryTransaction() = default;
			virtual TUniquePtr<IRegistryTransaction> Clone() const = 0;

			/** Returns the type of transaction */
			virtual ETransactionType GetTransactionType() const = 0;

			/** If a FNodeClassInfo added during the transaction, this will return
			 * a non-null pointer to the FNodeClassInfo. */
			virtual const FNodeClassInfo* GetNodeClassInfo() const = 0;
		};

		using FRegistryTransactionPtr = TUniquePtr<IRegistryTransaction>;

		/** Create a node registration transaction. */
		FRegistryTransactionPtr MakeAddNodeRegistrationTransaction(const FNodeClassInfo& InInfo);

		/** Create a node unregistration transaction. */
		FRegistryTransactionPtr MakeRemoveNodeRegistrationTransaction(const FNodeClassInfo& InInfo);

		/** Maintains a history of IRegistryTransactions. Calls are threadsafe (excluding
		 * the constructor and destructor.)
		 */
		class FRegistryTransactionHistory
		{
		public:

			FRegistryTransactionHistory();

			/** Add a transaction to the history. Threadsafe. 
			 *
			 * @return The transaction ID associated with the action. */
			FRegistryTransactionID Add(FRegistryTransactionPtr&& InRegistryTransaction);

			/** Add a transaction to the history. Threadsafe. 
			 *
			 * @return The transaction ID associated with the action. */
			FRegistryTransactionID Add(const IRegistryTransaction& InRegistryTransaction);

			/** Gets the transaction ID of the most recent transaction. Threadsafe. */
			FRegistryTransactionID GetCurrent() const;

			/** Returns an array of transaction pointers. Threadsafe. 
			 *
			 * @param InSince - All transactions occuring after this transaction ID will be returned.
			 * @param OutCurrent - If not null, will be set to the most recent transaction ID returned. 
			 */
			TArray<const IRegistryTransaction*> GetTransactions(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrent) const;

		private:

			mutable FCriticalSection RegistryTransactionMutex;

			FRegistryTransactionID Current;

			TMap<FRegistryTransactionID, int32> RegistryTransactionIndexMap;
			TArray<FRegistryTransactionPtr> RegistryTransactions;
		};
	}
}


