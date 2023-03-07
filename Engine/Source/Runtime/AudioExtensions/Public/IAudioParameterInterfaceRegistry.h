// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioParameter.h"
#include "AudioParameterControllerInterface.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Misc/AssertionMacros.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"


namespace Audio
{
	// Forward Declarations
	class IAudioParameterInterfaceRegistry;

	struct AUDIOEXTENSIONS_API FParameterInterface
	{
		struct AUDIOEXTENSIONS_API FVersion
		{
			const int32 Major = 1;
			const int32 Minor = 0;
		};

		struct AUDIOEXTENSIONS_API FInput
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FAudioParameter InitValue;
			const FText RequiredText;

			const int32 SortOrderIndex = 0;
		};

		struct AUDIOEXTENSIONS_API FOutput
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FName ParamName;
			const FText RequiredText = FText();
			const EAudioParameterType ParamType = EAudioParameterType::None;

			const int32 SortOrderIndex = 0;
		};

		struct AUDIOEXTENSIONS_API FEnvironmentVariable
		{
			const FText DisplayName;
			const FText Description;
			const FName DataType;

			const FName ParamName;
			const EAudioParameterType ParamType = EAudioParameterType::None;
		};

		FParameterInterface() = default;

		// Constructor used for parameter interface not limited to any particular UClass types
		FParameterInterface(FName InName, const FVersion& InVersion);

		// Constructor used for parameter interface with support for explicit UClass types
		FParameterInterface(FName InName, const FVersion& InVersion, const TArray<UClass*>& InClasses);

		UE_DEPRECATED(5.3, "Use FParameterInterface ctor that supports array of classes.")
		FParameterInterface(FName InName, const FVersion& InVersion, const UClass& InClass);

		FName GetName() const;
		const FVersion& GetVersion() const;

		UE_DEPRECATED(5.3, "Use FParameterInterface::FindSupportedUClasses instead")
		const UClass& GetType() const;

		TArray<const UClass*> FindSupportedUClasses() const;
		const TArray<FInput>& GetInputs() const;
		const TArray<FOutput>& GetOutputs() const;
		const TArray<FEnvironmentVariable>& GetEnvironment() const;

	private:
		FName NamePrivate;
		FVersion VersionPrivate;
		TArray<FString> SupportedUClassNames;

	protected:
		TArray<FInput> Inputs;
		TArray<FOutput> Outputs;
		TArray<FEnvironmentVariable> Environment;
	};
	using FParameterInterfacePtr = TSharedPtr<FParameterInterface, ESPMode::ThreadSafe>;

	class AUDIOEXTENSIONS_API IAudioParameterInterfaceRegistry
	{
		static TUniquePtr<IAudioParameterInterfaceRegistry> Instance;

	public:
		static IAudioParameterInterfaceRegistry& Get();

		virtual ~IAudioParameterInterfaceRegistry() = default;

		virtual void IterateInterfaces(TFunction<void(FParameterInterfacePtr)> InFunction) const = 0;
		virtual void OnRegistration(TUniqueFunction<void(FParameterInterfacePtr)>&& InFunction) = 0;
		virtual void RegisterInterface(FParameterInterfacePtr InInterface) = 0;

	protected:
		TSet<FParameterInterfacePtr> Interfaces;
		TUniqueFunction<void(FParameterInterfacePtr)> RegistrationFunction;
	};
} // namespace Audio
