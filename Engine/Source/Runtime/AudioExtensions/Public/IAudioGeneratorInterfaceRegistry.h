// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioParameterInterface.h"
#include "Templates/Function.h"
#include "UObject/Class.h"


namespace Audio
{
	class IGeneratorInterfaceRegistry;

	struct AUDIOEXTENSIONS_API FGeneratorInterface
	{
		FGeneratorInterface() = default;
		FGeneratorInterface(FName InName, UClass* InType)
			: Name(InName)
			, Type(InType)
		{
		}

		FName Name;
		UClass* Type = nullptr;

		struct FVersion
		{
			int32 Major = 1;
			int32 Minor = 0;
		} Version;

		struct FInput
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FAudioParameter InitValue;
		};
		TArray<FInput> Inputs;

		struct FOutput
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FName ParamName;
			const EAudioParameterType ParamType;
		};
		TArray<FOutput> Outputs;

		struct FEnvironmentVariable
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FName ParamName;
			const EAudioParameterType ParamType;
		};
		TArray<FEnvironmentVariable> Environment;
	};
	using FGeneratorInterfacePtr = TSharedPtr<FGeneratorInterface>;

	class AUDIOEXTENSIONS_API IGeneratorInterfaceRegistry
	{
		static TUniquePtr<IGeneratorInterfaceRegistry> Instance;

	public:
		static const FString NamespaceDelimiter;
		static IGeneratorInterfaceRegistry& Get();

		static FName GetMemberFullName(FName InInterfaceName, FName InParameterName);
		static void SplitMemberFullName(FName InFullName, FName& OutInterfaceName, FName& OutParameterName);

		virtual ~IGeneratorInterfaceRegistry() = default;

		virtual void IterateInterfaces(TFunction<void(FGeneratorInterfacePtr)> InFunction) const = 0;
		virtual void OnRegistration(TUniqueFunction<void(FGeneratorInterfacePtr)>&& InFunction) = 0;
		virtual void RegisterInterface(FGeneratorInterfacePtr InInterface) = 0;

	protected:
		TSet<FGeneratorInterfacePtr> Interfaces;
		TUniqueFunction<void(FGeneratorInterfacePtr)> RegistrationFunction;
	};
} // namespace Audio
