// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendArchetypeRegistry.h"

#include "HAL/PlatformTime.h"
#include "MetasoundFrontendRegistryTransaction.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundFrontendArchetypeRegistryPrivate
		{
			class FInterfaceRegistry : public IInterfaceRegistry
			{
			public:
				virtual ~FInterfaceRegistry() = default;

				virtual void RegisterInterface(TUniquePtr<IInterfaceRegistryEntry>&& InEntry) override
				{
					FInterfaceRegistryTransaction::FTimeType TransactionTime = FPlatformTime::Cycles64();
					if (InEntry.IsValid())
					{
						FInterfaceRegistryKey Key = GetInterfaceRegistryKey(InEntry->GetInterface());
						if (IsValidInterfaceRegistryKey(Key))
						{
							if (const IInterfaceRegistryEntry* Entry = FindInterfaceRegistryEntry(Key))
							{
								UE_LOG(LogMetaSound, Warning, TEXT("Registration of interface overwriting previously registered interface [RegistryKey: %s]"), *Key);
								
								FInterfaceRegistryTransaction Transaction{FInterfaceRegistryTransaction::ETransactionType::InterfaceUnregistration, Key, Entry->GetInterface().Version, TransactionTime};
								Transactions.Add(MoveTemp(Transaction));
							}
							
							FInterfaceRegistryTransaction Transaction{FInterfaceRegistryTransaction::ETransactionType::InterfaceRegistration, Key, InEntry->GetInterface().Version, TransactionTime};
							Transactions.Add(MoveTemp(Transaction));

							Entries.Add(Key, MoveTemp(InEntry)).Get();
						}
					}
				}

				virtual const IInterfaceRegistryEntry* FindInterfaceRegistryEntry(const FInterfaceRegistryKey& InKey) const override
				{
					if (const TUniquePtr<IInterfaceRegistryEntry>* Entry = Entries.Find(InKey))
					{
						return Entry->Get();
					}
					return nullptr;
				}

				virtual bool FindInterface(const FInterfaceRegistryKey& InKey, FMetasoundFrontendInterface& OutInterface) const override
				{
					if (const IInterfaceRegistryEntry* Entry = FindInterfaceRegistryEntry(InKey))
					{
						OutInterface = Entry->GetInterface();
						return true;
					}

					return false;
				}

				virtual void ForEachRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FInterfaceRegistryTransaction&)> InFunc) const override
				{
					Transactions.ForEachTransactionSince(InSince, OutCurrentRegistryTransactionID, InFunc);
				}

			private:

				TMap<FInterfaceRegistryKey, TUniquePtr<IInterfaceRegistryEntry>> Entries;
				TRegistryTransactionHistory<FInterfaceRegistryTransaction> Transactions;
			};
		}

		bool IsValidInterfaceRegistryKey(const FInterfaceRegistryKey& InKey)
		{
			return !InKey.IsEmpty();
		}

		FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendVersion& InInterfaceVersion)
		{
			return FString::Format(TEXT("{0}_{1}.{2}"), { InInterfaceVersion.Name.ToString(), InInterfaceVersion.Number.Major, InInterfaceVersion.Number.Minor });

		}

		FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendInterface& InInterface)
		{
			return GetInterfaceRegistryKey(InInterface.Version);
		}

		FInterfaceRegistryTransaction::FInterfaceRegistryTransaction(ETransactionType InType, const FInterfaceRegistryKey& InKey, const FMetasoundFrontendVersion& InInterfaceVersion, FInterfaceRegistryTransaction::FTimeType InTimestamp)
		: Type(InType)
		, Key(InKey)
		, InterfaceVersion(InInterfaceVersion)
		, Timestamp(InTimestamp)
		{
		}

		FInterfaceRegistryTransaction::ETransactionType FInterfaceRegistryTransaction::GetTransactionType() const
		{
			return Type;
		}

		const FMetasoundFrontendVersion& FInterfaceRegistryTransaction::GetInterfaceVersion() const
		{
			return InterfaceVersion;
		}

		const FInterfaceRegistryKey& FInterfaceRegistryTransaction::GetInterfaceRegistryKey() const
		{
			return Key;
		}

		FInterfaceRegistryTransaction::FTimeType FInterfaceRegistryTransaction::GetTimestamp() const
		{
			return Timestamp;
		}

		IInterfaceRegistry& IInterfaceRegistry::Get()
		{
			static MetasoundFrontendArchetypeRegistryPrivate::FInterfaceRegistry Registry;
			return Registry;
		}
	}
}
