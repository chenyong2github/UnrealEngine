// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/NoExportTypes.h"

namespace Metasound
{
	namespace Frontend
	{
		using FInterfaceRegistryKey = FString;
		using FRegistryTransactionID = int32;

		METASOUNDFRONTEND_API bool IsValidInterfaceRegistryKey(const FInterfaceRegistryKey& InKey);
		METASOUNDFRONTEND_API FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendVersion& InInterfaceVersion);
		METASOUNDFRONTEND_API FInterfaceRegistryKey GetInterfaceRegistryKey(const FMetasoundFrontendInterface& InInterface);

		class METASOUNDFRONTEND_API IInterfaceRegistryEntry
		{
		public:
			virtual ~IInterfaceRegistryEntry() = default;

			// MetaSound Interface definition
			virtual const FMetasoundFrontendInterface& GetInterface() const = 0;

			// Whether or not to immediately apply the given interface
			// when a MetaSound asset of the supported UClass is created.
			virtual bool IsDefault(FName InUClassName) const = 0;

			UE_DEPRECATED(5.3, "UClass name must now be provided to determine if interface is to be apply by default.")
			virtual bool IsDefault() const { return false; }

			// Whether or not the interface can be added or removed by the editor.
			virtual bool EditorCanAddOrRemove(FName InUClassName) const = 0;

			UE_DEPRECATED(5.3, "UClass name must now be provided to determine if editor can directly add or remove interface.")
			virtual bool EditorCanAddOrRemove() const { return false; }

			// Name of routing system used to update interface inputs (ex. ParameterInterface or DataReference).
			virtual FName GetRouterName() const = 0;

			// Name of UClass supported by the given interface.
			virtual bool UClassIsSupported(FName InUClassName) const = 0;

			// How to update a given document if versioning is required to this interface from a deprecated version.
			virtual bool UpdateRootGraphInterface(FDocumentHandle InDocument) const = 0;

			// Adds bindings that informs how this interface can be used to connect its inputs to node that implements another interface's outputs. Faster than adding iteratively.
			virtual void AddOutputBindings(TArray<FMetasoundFrontendInterfaceBinding>&& InBindings) = 0;

			// Returns all bindings that inform how this interface can be used to connect its inputs to a node that implements another interface's outputs.
			virtual const TArray<FMetasoundFrontendInterfaceBinding>& GetOutputBindings() const = 0;

			// Remove binding that informs how this interface can be used to connect its inputs to node that implements another interface's outputs.
			virtual bool RemoveOutputBinding(const FMetasoundFrontendVersion& InVersion) = 0;

			// Resets output bindings collection.
			virtual void ResetOutputBindings() = 0;
		};

		class METASOUNDFRONTEND_API FInterfaceRegistryTransaction
		{
		public:
			using FTimeType = uint64;

			/** Describes the type of transaction. */
			enum class ETransactionType : uint8
			{
				InterfaceRegistration,     //< Something was added to the registry.
				InterfaceUnregistration,  //< Something was removed from the registry.
				Invalid
			};

			FInterfaceRegistryTransaction(ETransactionType InType, const FInterfaceRegistryKey& InKey, const FMetasoundFrontendVersion& InInterfaceVersion, FTimeType InTimestamp);

			ETransactionType GetTransactionType() const;
			const FMetasoundFrontendVersion& GetInterfaceVersion() const;
			const FInterfaceRegistryKey& GetInterfaceRegistryKey() const;
			FTimeType GetTimestamp() const;

		private:

			ETransactionType Type;
			FInterfaceRegistryKey Key;
			FMetasoundFrontendVersion InterfaceVersion;
			FTimeType Timestamp;
		};

		class METASOUNDFRONTEND_API IInterfaceRegistry
		{
		public:
			static IInterfaceRegistry& Get();

			virtual ~IInterfaceRegistry() = default;

			// Register an interface
			virtual void RegisterInterface(TUniquePtr<IInterfaceRegistryEntry>&& InEntry) = 0;

			// Find an interface entry with the given key. Returns null if entry not found with given key.
			virtual const IInterfaceRegistryEntry* FindInterfaceRegistryEntry(const FInterfaceRegistryKey& InKey) const = 0;

			// Find an interface with the given key. Returns true if interface is found, false if not.
			virtual bool FindInterface(const FInterfaceRegistryKey& InKey, FMetasoundFrontendInterface& OutInterface) const = 0;

			// Add interface output bindings to the interface with the given key. Orders the bindings using the given sort order, where lower values are evaluated earlier if multiple bindings are provided.
			virtual void AddInterfaceOutputBindings(const FMetasoundFrontendVersion& InInputInterfaceVersion, TArray<FMetasoundFrontendInterfaceBinding>&& InOutputBindings) = 0;

			// Removes an interface binding to the input interface with the given key from the interface with the given key. Returns true if interface binding was removed, false if not.
			virtual bool RemoveInterfaceOutputBinding(const FMetasoundFrontendVersion& InInputInterfaceVersion, const FMetasoundFrontendVersion& InOutputInterfaceVersion) = 0;

			// Removes all output interface bindings registered with the given interface version. Returns true if found and removed, false if not found.
			virtual bool RemoveAllInterfaceOutputBindings(const FMetasoundFrontendVersion& InInputInterfaceVersion) = 0;

			// Executes a function against all transactions since a provided ID and provides the current transaction ID.
			UE_DEPRECATED(5.1, "ForEachRegistryTransactionSince is no longer supported. Use Metasound::Frontend::ISearchEngine to query for all interfaces.")
			virtual void ForEachRegistryTransactionSince(FRegistryTransactionID InSince, FRegistryTransactionID* OutCurrentRegistryTransactionID, TFunctionRef<void(const FInterfaceRegistryTransaction&)> InFunc) const = 0;
		};
	}
}
