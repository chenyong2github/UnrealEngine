// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"

namespace Metasound
{
	namespace Frontend
	{
		using FArchetypeRegistryKey = FString;
		using FRegistryTransactionID = int32;

		METASOUNDFRONTEND_API bool IsValidArchetypeRegistryKey(const FArchetypeRegistryKey& InKey);
		METASOUNDFRONTEND_API FArchetypeRegistryKey GetArchetypeRegistryKey(const FMetasoundFrontendVersion& InArchetypeVersion);
		METASOUNDFRONTEND_API FArchetypeRegistryKey GetArchetypeRegistryKey(const FMetasoundFrontendArchetype& InArchetype);

		class IArchetypeRegistryEntry
		{
		public:
			virtual ~IArchetypeRegistryEntry() = default;

			virtual const FMetasoundFrontendArchetype& GetArchetype() const = 0;
			virtual bool UpdateRootGraphArchetype(FDocumentHandle InDocument) const = 0;
		};

		class METASOUNDFRONTEND_API FArchetypeRegistryTransaction 
		{
		public:
			/** Describes the type of transaction. */
			enum class ETransactionType : uint8
			{
				ArchetypeRegistration,     //< Something was added to the registry.
				ArchetypeUnregistration,  //< Something was removed from the registry.
				Invalid
			};

			FArchetypeRegistryTransaction(ETransactionType InType, const FArchetypeRegistryKey& InKey, const FMetasoundFrontendVersion& InArchetypeVersion);

			ETransactionType GetTransactionType() const;
			const FMetasoundFrontendVersion& GetArchetypeVersion() const;
			const FArchetypeRegistryKey& GetArchetypeRegistryKey() const;

		private:

			ETransactionType Type;
			FArchetypeRegistryKey Key;
			FMetasoundFrontendVersion ArchetypeVersion;
		};

		class METASOUNDFRONTEND_API IArchetypeRegistry
		{
		public:
			static IArchetypeRegistry& Get();

			virtual ~IArchetypeRegistry() = default;

			virtual FArchetypeRegistryKey RegisterArchetype(TUniquePtr<IArchetypeRegistryEntry>&& InEntry) = 0;
			virtual const IArchetypeRegistryEntry* FindArchetypeRegistryEntry(const FArchetypeRegistryKey& InKey) const = 0;
			virtual bool FindArchetype(const FArchetypeRegistryKey& InKey, FMetasoundFrontendArchetype& OutArchetype) const = 0;
			virtual void ForEachRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FArchetypeRegistryTransaction&)> InFunc) const = 0;
		};
	}
}
