// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundFrontendRegistryTransaction.h"

#include "CoreMinimal.h"

namespace Metasound
{
	namespace Frontend
	{
		FRegistryTransactionID GetOriginRegistryTransactionID()
		{
			return 0;
		}

		FRegistryTransactionHistory::FRegistryTransactionHistory()
		: Current(GetOriginRegistryTransactionID())
		{
		}

		FRegistryTransactionID FRegistryTransactionHistory::Add(TUniquePtr<IRegistryTransaction>&& InRegistryTransaction)
		{
			FScopeLock Lock(&RegistryTransactionMutex);

			if (ensure(InRegistryTransaction.IsValid()))
			{
				Current++;

				IRegistryTransaction* RegistryTransactionPointer = InRegistryTransaction.Get();
				int32 Index = RegistryTransactions.Num();

				RegistryTransactions.Add(MoveTemp(InRegistryTransaction));
				RegistryTransactionIndexMap.Add(Current, Index);
			}

			return Current;
		}

		FRegistryTransactionID FRegistryTransactionHistory::Add(const IRegistryTransaction& InRegistryTransaction)
		{
			return Add(InRegistryTransaction.Clone());
		}

		FRegistryTransactionID FRegistryTransactionHistory::GetCurrent() const
		{
			FScopeLock Lock(&RegistryTransactionMutex);
			{
				return Current;
			}
		}

		TArray<const IRegistryTransaction*> FRegistryTransactionHistory::GetTransactions(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrent) const
		{
			FScopeLock Lock(&RegistryTransactionMutex);
			{
				TArray<const IRegistryTransaction*> Result;

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
							Result.Reserve(OutNum);
							for (int32 i = Start; i < Num; i++)
							{
								Result.Add(RegistryTransactions[i].Get());
							}
						}
					}
				}

				return Result;
			}
			
		}

		FRegistryTransactionPtr MakeAddNodeRegistrationTransaction(const FNodeClassInfo& InInfo)
		{
			class FNodeRegistrationTransaction : public IRegistryTransaction
			{
			public:
				FNodeRegistrationTransaction(const FNodeClassInfo& InNodeClassInfo)
				: NodeClassInfo(InNodeClassInfo)
				{
				}

				ETransactionType GetTransactionType() const override
				{
					return ETransactionType::Add;
				}

				TUniquePtr<IRegistryTransaction> Clone() const override
				{
					return MakeUnique<FNodeRegistrationTransaction>(*this);
				}

				const FNodeClassInfo* GetNodeClassInfo() const 
				{
					return &NodeClassInfo;
				}

			private:

				FNodeClassInfo NodeClassInfo;
			};

			return MakeUnique<FNodeRegistrationTransaction>(InInfo);
		}

		FRegistryTransactionPtr MakeRemoveNodeRegistrationTransaction(const FNodeClassInfo& InInfo)
		{
			class FNodeRegistrationTransaction : public IRegistryTransaction
			{
			public:
				FNodeRegistrationTransaction(const FNodeClassInfo& InNodeClassInfo)
				: NodeClassInfo(InNodeClassInfo)
				{
				}

				ETransactionType GetTransactionType() const override
				{
					return ETransactionType::Remove;
				}

				TUniquePtr<IRegistryTransaction> Clone() const override
				{
					return MakeUnique<FNodeRegistrationTransaction>(*this);
				}

				const FNodeClassInfo* GetNodeClassInfo() const 
				{
					return &NodeClassInfo;
				}

			private:

				FNodeClassInfo NodeClassInfo;
			};

			return MakeUnique<FNodeRegistrationTransaction>(InInfo);
		}
	}
}
