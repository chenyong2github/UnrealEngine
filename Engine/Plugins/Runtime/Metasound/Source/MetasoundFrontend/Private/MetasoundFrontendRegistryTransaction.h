// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"

namespace Metasound
{
	namespace Frontend
	{
		/** Returns an ID representing the beginning of the transaction history. */
		FRegistryTransactionID GetOriginRegistryTransactionID();

		/** Create a node registration transaction. */
		FRegistryTransactionPtr MakeAddNodeRegistryTransaction(const FNodeClassInfo& InInfo);

		/** Create a node unregistration transaction. */
		FRegistryTransactionPtr MakeRemoveNodeRegistryTransaction(const FNodeClassInfo& InInfo);

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


