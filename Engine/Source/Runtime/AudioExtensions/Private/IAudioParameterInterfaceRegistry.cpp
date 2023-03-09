// Copyright Epic Games, Inc. All Rights Reserved.
#include "IAudioParameterInterfaceRegistry.h"

#include "Algo/Transform.h"


namespace Audio
{
	namespace ParameterInterfaceRegistryPrivate
	{
		class FParameterInterfaceRegistry : public IAudioParameterInterfaceRegistry
		{
		public:
			virtual void IterateInterfaces(TFunction<void(FParameterInterfacePtr)> InFunction) const override
			{
				for (const FParameterInterfacePtr& InterfacePtr : Interfaces)
				{
					InFunction(InterfacePtr);
				}
			}

			virtual void RegisterInterface(FParameterInterfacePtr InInterface) override
			{
				Interfaces.Add(InInterface);
				if (RegistrationFunction)
				{
					RegistrationFunction(InInterface);
				}
			}

			virtual void OnRegistration(TUniqueFunction<void(FParameterInterfacePtr)>&& InFunction) override
			{
				RegistrationFunction = MoveTemp(InFunction);
			}
		};
	} // namespace ParameterInterfaceRegistryPrivate

	FParameterInterface::FParameterInterface(
		FName InName,
		const FVersion& InVersion,
		const UClass& InType
	)
		: NamePrivate(InName)
		, VersionPrivate(InVersion)
		, SupportedUClassNames({ InType.GetPathName() })
	{
	}

	FParameterInterface::FParameterInterface(FName InName, const FVersion& InVersion)
		: NamePrivate(InName)
		, VersionPrivate(InVersion)
	{
	}

	FParameterInterface::FParameterInterface(FName InName, const FVersion& InVersion, const TArray<UClass*>& InClasses)
		: NamePrivate(InName)
		, VersionPrivate(InVersion)
	{
		Algo::Transform(InClasses, SupportedUClassNames, [](const UClass* Class)
		{
			check(Class);
			return Class->GetPathName();
		});
	}

	FName FParameterInterface::GetName() const
	{
		return NamePrivate;
	}

	const FParameterInterface::FVersion& FParameterInterface::GetVersion() const
	{
		return VersionPrivate;
	}

	const UClass& FParameterInterface::GetType() const
	{
		return *UObject::StaticClass();
	}

	const TArray<FParameterInterface::FInput>& FParameterInterface::GetInputs() const
	{
		return Inputs;
	}

	const TArray<FParameterInterface::FOutput>& FParameterInterface::GetOutputs() const
	{
		return Outputs;
	}

	const TArray<FParameterInterface::FEnvironmentVariable>& FParameterInterface::GetEnvironment() const
	{
		return Environment;
	}

	TArray<const UClass*> FParameterInterface::FindSupportedUClasses() const
	{
		TArray<const UClass*> SupportedUClasses;
		for (const FString& Name : SupportedUClassNames)
		{
			if (const UClass* Class = FindFirstObject<const UClass>(*Name, EFindFirstObjectOptions::ExactClass, ELogVerbosity::Warning, TEXT("FParameterInterface::FindSupportedUClasses")))
			{
				SupportedUClasses.Add(Class);
			}
		}

		return SupportedUClasses;
	}

	TUniquePtr<IAudioParameterInterfaceRegistry> IAudioParameterInterfaceRegistry::Instance;

	IAudioParameterInterfaceRegistry& IAudioParameterInterfaceRegistry::Get()
	{
		using namespace ParameterInterfaceRegistryPrivate;

		if (!Instance.IsValid())
		{
			Instance = MakeUnique<FParameterInterfaceRegistry>();
		}
		return *Instance;
	}
} // namespace Audio
