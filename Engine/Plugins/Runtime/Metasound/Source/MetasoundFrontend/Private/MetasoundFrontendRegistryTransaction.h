// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendRegistries.h"
#include "Algo/ForEach.h"

namespace Metasound
{
	namespace Frontend
	{
		/** Returns an ID representing the beginning of the transaction history. */
		FRegistryTransactionID GetOriginRegistryTransactionID();


		/** Maintains a history of TransactionTypes. Calls are threadsafe (excluding
		 * the constructor and destructor.)
		 */
		template<typename TransactionType>
		class TRegistryTransactionHistory
		{
		public:
			TRegistryTransactionHistory()
			: Current(GetOriginRegistryTransactionID())
			{
			}

			/** Add a transaction to the history. Threadsafe. 
			 *
			 * @return The transaction ID associated with the action. */
			FRegistryTransactionID Add(TransactionType&& InRegistryTransaction)
			{
				FScopeLock Lock(&RegistryTransactionMutex);

				Current++;

				int32 Index = RegistryTransactions.Num();

				RegistryTransactions.Add(MoveTemp(InRegistryTransaction));
				RegistryTransactionIndexMap.Add(Current, Index);

				return Current;
			}

			/** Gets the transaction ID of the most recent transaction. Threadsafe. */
			FRegistryTransactionID GetCurrent() const
			{
				FScopeLock Lock(&RegistryTransactionMutex);
				{
					return Current;
				}
			}

			/** Invoke a function on all transactions since transaction ID. 
			 *
			 * @param InSince - All transactions occurring after this transaction ID will be returned.
			 * @param OutCurrent - If not null, will be set to the most recent transaction ID returned. 
			 * @param InCallable - A callable of the form Callable(const TransactionType&)
			 */
			template<typename CallableType>
			void ForEachTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrent, CallableType InCallable) const
			{
				FScopeLock Lock(&RegistryTransactionMutex);
				{
					if (nullptr != OutCurrent)
					{
						*OutCurrent = Current;
					}

					int32 Start = INDEX_NONE;
					
					if (GetOriginRegistryTransactionID() == InSince)
					{
						Start = 0;
					}
					else if (const int32* Pos = RegistryTransactionIndexMap.Find(InSince))
					{
						Start = *Pos + 1;
					}
					
					if (INDEX_NONE != Start)
					{
						const int32 Num = RegistryTransactions.Num();

						if (ensure(Start <= Num))
						{
							const int32 OutNum = Num - Start;
							if (OutNum > 0)
							{
								TArrayView<const TransactionType> TransactionsSince = MakeArrayView(&RegistryTransactions[Start], OutNum);
								Algo::ForEach(TransactionsSince, InCallable);
							}
						}
					}
				}
			}

		private:

			mutable FCriticalSection RegistryTransactionMutex;

			FRegistryTransactionID Current;

			TMap<FRegistryTransactionID, int32> RegistryTransactionIndexMap;
			TArray<TransactionType> RegistryTransactions;
		};
	}
}


