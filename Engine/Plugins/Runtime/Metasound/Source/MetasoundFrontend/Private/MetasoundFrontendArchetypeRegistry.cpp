// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendRegistryTransaction.h"

namespace Metasound
{
	namespace Frontend
	{
		namespace MetasoundFrontendArchetypeRegistryPrivate
		{
			class FArchetypeRegistry : public IArchetypeRegistry
			{
			public:
				virtual ~FArchetypeRegistry() = default;

				virtual FArchetypeRegistryKey RegisterArchetype(TUniquePtr<IArchetypeRegistryEntry>&& InEntry) override
				{
					if (InEntry.IsValid())
					{
						FArchetypeRegistryKey Key = GetArchetypeRegistryKey(InEntry->GetArchetype());
						if (IsValidArchetypeRegistryKey(Key))
						{
							if (const IArchetypeRegistryEntry* Entry = FindArchetypeRegistryEntry(Key))
							{
								UE_LOG(LogMetaSound, Warning, TEXT("Registration of archetype overwriting previously registered archetype [RegistryKey: %s]"), *Key);
								
								FArchetypeRegistryTransaction Transaction{FArchetypeRegistryTransaction::ETransactionType::ArchetypeUnregistration, Key, Entry->GetArchetype().Version};
								Transactions.Add(MoveTemp(Transaction));
							}
							
							FArchetypeRegistryTransaction Transaction{FArchetypeRegistryTransaction::ETransactionType::ArchetypeRegistration, Key, InEntry->GetArchetype().Version};
							Transactions.Add(MoveTemp(Transaction));

							Entries.Add(Key, MoveTemp(InEntry));

							return Key;
						}
					}

					return FArchetypeRegistryKey{};
				}

				virtual const IArchetypeRegistryEntry* FindArchetypeRegistryEntry(const FArchetypeRegistryKey& InKey) const override
				{
					if (const TUniquePtr<IArchetypeRegistryEntry>* Entry = Entries.Find(InKey))
					{
						return Entry->Get();
					}
					return nullptr;
				}

				virtual bool FindArchetype(const FArchetypeRegistryKey& InKey, FMetasoundFrontendArchetype& OutArchetype) const override
				{
					if (const IArchetypeRegistryEntry* Entry = FindArchetypeRegistryEntry(InKey))
					{
						OutArchetype = Entry->GetArchetype();
						return true;
					}

					return false;
				}

				virtual void ForEachRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FArchetypeRegistryTransaction&)> InFunc) const override
				{
					Transactions.ForEachTransactionSince(InSince, OutCurrentRegistryTransactionID, InFunc);
				}

			private:

				TMap<FArchetypeRegistryKey, TUniquePtr<IArchetypeRegistryEntry>> Entries;
				TRegistryTransactionHistory<FArchetypeRegistryTransaction> Transactions;
			};
		}

		bool IsValidArchetypeRegistryKey(const FArchetypeRegistryKey& InKey)
		{
			return !InKey.IsEmpty();
		}

		FArchetypeRegistryKey GetArchetypeRegistryKey(const FMetasoundFrontendVersion& InArchetypeVersion)
		{
			return FString::Format(TEXT("{0}_{1}.{2}"), { InArchetypeVersion.Name.ToString(), InArchetypeVersion.Number.Major, InArchetypeVersion.Number.Minor });

		}

		FArchetypeRegistryKey GetArchetypeRegistryKey(const FMetasoundFrontendArchetype& InArchetype)
		{
			return GetArchetypeRegistryKey(InArchetype.Version);
		}

		FArchetypeRegistryTransaction::FArchetypeRegistryTransaction(ETransactionType InType, const FArchetypeRegistryKey& InKey, const FMetasoundFrontendVersion& InArchetypeVersion)
		: Type(InType)
		, Key(InKey)
		, ArchetypeVersion(InArchetypeVersion)
		{
		}

		FArchetypeRegistryTransaction::ETransactionType FArchetypeRegistryTransaction::GetTransactionType() const
		{
			return Type;
		}

		const FMetasoundFrontendVersion& FArchetypeRegistryTransaction::GetArchetypeVersion() const
		{
			return ArchetypeVersion;
		}

		const FArchetypeRegistryKey& FArchetypeRegistryTransaction::GetArchetypeRegistryKey() const
		{
			return Key;
		}

		IArchetypeRegistry& IArchetypeRegistry::Get()
		{
			static MetasoundFrontendArchetypeRegistryPrivate::FArchetypeRegistry Registry;
			return Registry;
		}
	}
}
