// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundAssetBase.h"
#include "UObject/Object.h"

namespace Metasound
{
	/** Interface for an entry into the Metasound-UObject Registry. 
	 *
	 * An entry provides information linking a FMetasoundFrontendArchetype to a UClass.
	 * It also provides methods for accessing the FMetasoundAssetBase from a UObject.
	 */
	class IMetasoundUObjectRegistryEntry
	{
	public:
		virtual ~IMetasoundUObjectRegistryEntry() = default;

		/** Archetype name associated with this entry. */
		virtual const FName& GetArchetypeName() const = 0;

		/** UClass associated with this entry. */
		virtual UClass* GetUClass() const = 0;

		/** Returns true if the UObject is of a Class which is a child of this UClass associated with this entry. */
		virtual bool IsChildClass(const UObject* InObject) const = 0;

		/** Returns true if the UClass is a child of this UClass associated with this entry. */
		virtual bool IsChildClass(const UClass* InClass) const = 0;

		/** Attempts to cast the UObject to an FMetasoundAssetBase */
		virtual FMetasoundAssetBase* Cast(UObject* InObject) const = 0;

		/** Attempts to cast the UObject to an FMetasoundAssetBase */
		virtual const FMetasoundAssetBase* Cast(const UObject* InObject) const = 0;

		/** Creates a new object of the UClass type. */
		virtual UObject* NewObject(UPackage* InPackage, const FName& InName) const = 0;

	private:
		IMetasoundUObjectRegistryEntry() = default;

		/** Only the TMetasoundUObjectRegistryEntry can construct this class. */
		template<typename UClassType>
		friend class TMetasoundUObjectRegistryEntry;
	};

	/** An entry into the Metasound-UObject registry. 
	 *
	 * @Tparam UClassType A class which derives from UObject and FMetasoundAssetBase.
	 */
	template<typename UClassType>
	class TMetasoundUObjectRegistryEntry : public IMetasoundUObjectRegistryEntry
	{
		// Ensure that this is a subclass of FMetasoundAssetBase and UObject.
		static_assert(std::is_base_of<FMetasoundAssetBase, UClassType>::value, "UClass must be derived from FMetasoundAssetBase");
		static_assert(std::is_base_of<UObject, UClassType>::value, "UClass must be derived from UObject");

	public:
		TMetasoundUObjectRegistryEntry(const FName& InArchetypeName)
		:	ArchetypeName(InArchetypeName)
		{
		}

		virtual ~TMetasoundUObjectRegistryEntry() = default;

		const FName& GetArchetypeName() const
		{
			return ArchetypeName;
		}

		UClass* GetUClass() const override
		{
			return UClassType::StaticClass();
		}

		bool IsChildClass(const UObject* InObject) const override
		{
			if (nullptr != InObject)
			{
				return InObject->IsA(UClassType::StaticClass());
			}
			return false;
		}

		bool IsChildClass(const UClass* InClass) const override
		{
			if (nullptr != InClass)
			{
				return InClass->IsChildOf(UClassType::StaticClass());
			}
			return false;
		}

		FMetasoundAssetBase* Cast(UObject* InObject) const override
		{
			if (nullptr == InObject)
			{
				return nullptr;
			}
			return static_cast<FMetasoundAssetBase*>(CastChecked<UClassType>(InObject));
		}

		const FMetasoundAssetBase* Cast(const UObject* InObject) const override
		{
			if (nullptr == InObject)
			{
				return nullptr;
			}
			return static_cast<const FMetasoundAssetBase*>(CastChecked<const UClassType>(InObject));
		}

		UObject* NewObject(UPackage* InPackage, const FName& InName) const override
		{
			return ::NewObject<UClassType>(InPackage, InName);
		}

	private:

		FName ArchetypeName;
	};


	/** IMetaoundUObjectRegistry contains IMetasoundUObjectRegistryEntrys. 
	 *
	 * Registered UObject classes can utilize the Metasound Editor. It also enables
	 * the creation of a UObject directly from a FMetasoundFrontendDocument.
	 */
	class METASOUNDENGINE_API IMetasoundUObjectRegistry
	{
		public:
			virtual ~IMetasoundUObjectRegistry() = default;

			/** Return static singleton instance of the registry. */
			static IMetasoundUObjectRegistry& Get();

			/** Register all preferred archetypes of the UClass. 
			 *
			 * All Archtypes returned by the default objects GetPreferredArchetypes() will be registered. 
			 */
			template<typename UClassType>
			static void RegisterUClassPreferredArchetypes()
			{
				static_assert(std::is_base_of<FMetasoundAssetBase, UClassType>::value, "UClass must be derived from FMetasoundAssetBase");

				const TArray<FMetasoundFrontendArchetype>& PreferredArchetypes = GetDefault<UClassType>()->GetPreferredArchetypes();

				for (const FMetasoundFrontendArchetype& Arch : PreferredArchetypes)
				{
					IMetasoundUObjectRegistry::RegisterUClassArchetype<UClassType>(Arch.Name);
				}
			}

			/** Register an archetype for the UClass. 
			 *
			 * @param InArchetypeName - An FMetasoundFrontendDocument to associate with the UClass.
			 */
			template<typename UClassType>
			static void RegisterUClassArchetype(const FName& InArchetypeName)
			{
				using FRegistryEntryType = TMetasoundUObjectRegistryEntry<UClassType>;

				IMetasoundUObjectRegistry::Get().RegisterUClassArchetype(MakeUnique<FRegistryEntryType>(InArchetypeName));
			}

			/** Adds an entry to the registry. */
			virtual void RegisterUClassArchetype(TUniquePtr<IMetasoundUObjectRegistryEntry>&& InEntry) = 0;

			/** Returns all UClasses registered to the archetype name. */
			virtual TArray<UClass*> GetUClassesForArchetype(const FName& InArchetypeName) const = 0;

			/** Creates a new object from a metasound document.
			 *
			 * @param InClass - A registered UClass to create.
			 * @param InDocument - The FMetasoundFrontendDocument to use when creating the class.
			 * @param InArchetype - The FMetasoundFrontendArchetype to use when creating the class.
			 * @param InPath - If in editor, the created asset will be stored at this content path.
			 *
			 * @return A new object. A nullptr on error.
			 */
			virtual UObject* NewObject(UClass* InClass, const FMetasoundFrontendDocument& InDocument, const FMetasoundFrontendArchetype& InArchetype, const FString& InPath) const = 0;

			/** Returns true if the InObject is of a class or child class which is registered. */
			virtual bool IsRegisteredClass(UObject* InObject) const = 0;

			/** Returns casts the UObject to a FMetasoundAssetBase if the UObject is of a registered type.
			 * If the UObject's UClass is not registered, then a nullptr is returned. 
			 */
			virtual FMetasoundAssetBase* GetObjectAsAssetBase(UObject* InObject) const = 0;

			/** Returns casts the UObject to a FMetasoundAssetBase if the UObject is of a registered type.
			 * If the UObject's UClass is not registered, then a nullptr is returned. 
			 */
			virtual const FMetasoundAssetBase* GetObjectAsAssetBase(const UObject* InObject) const = 0;
	};
}
