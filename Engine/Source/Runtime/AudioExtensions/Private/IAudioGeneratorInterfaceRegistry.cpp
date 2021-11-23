// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioGeneratorInterfaceRegistry.h"

#include "Containers/Map.h"


namespace Audio
{
	namespace ParameterInterfaceRegistryPrivate
	{
		class FGeneratorInterfaceRegistry : public IGeneratorInterfaceRegistry
		{
		public:
			virtual void IterateInterfaces(TFunction<void(FGeneratorInterfacePtr)> InFunction) const override
			{
				for (const FGeneratorInterfacePtr& InterfacePtr : Interfaces)
				{
					InFunction(InterfacePtr);
				}
			}

			virtual void RegisterInterface(FGeneratorInterfacePtr InInterface) override
			{
				Interfaces.Add(InInterface);
				if (RegistrationFunction)
				{
					RegistrationFunction(InInterface);
				}
			}

			virtual void OnRegistration(TUniqueFunction<void(FGeneratorInterfacePtr)>&& InFunction) override
			{
				RegistrationFunction = MoveTemp(InFunction);
			}
		};
	} // namespace ParameterInterfaceRegistryPrivate

	TUniquePtr<IGeneratorInterfaceRegistry> IGeneratorInterfaceRegistry::Instance;

	const FString IGeneratorInterfaceRegistry::NamespaceDelimiter = TEXT(".");

	FName IGeneratorInterfaceRegistry::GetMemberFullName(FName InInterfaceName, FName InParameterName)
	{
		const FString FullName = FString::Join(TArray<FString>({ *InInterfaceName.ToString(), *InParameterName.ToString() }), *NamespaceDelimiter);
		return FName(FullName);
	}

	void IGeneratorInterfaceRegistry::SplitMemberFullName(FName InFullName, FName& OutInterfaceName, FName& OutParameterName)
	{
		FString FullName = InFullName.ToString();
		const int32 IndexOfDelim = FullName.Find(NamespaceDelimiter, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (IndexOfDelim != INDEX_NONE)
		{
			OutInterfaceName = FName(*FullName.Left(IndexOfDelim));
			OutParameterName = FName(*FullName.RightChop(IndexOfDelim + 1));
		}
		else
		{
			OutInterfaceName = FName();
			OutParameterName = InFullName;
		}
	}

	IGeneratorInterfaceRegistry& IGeneratorInterfaceRegistry::Get()
	{
		using namespace ParameterInterfaceRegistryPrivate;

		if (!Instance.IsValid())
		{
			Instance = MakeUnique<FGeneratorInterfaceRegistry>();
		}
		return *Instance;
	}
} // namespace Audio
