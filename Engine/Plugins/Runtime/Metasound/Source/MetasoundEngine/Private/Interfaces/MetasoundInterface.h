// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioParameterInterfaceRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendTransform.h"


namespace Metasound::Engine
{
	struct FInterfaceRegistryUClassOptions
	{
		FName ClassName;
		bool bIsDefault = false;
		bool bEditorCanAddOrRemove = false;
	};

	struct FInterfaceRegistryOptions
	{
		FName InputSystemName;
		TArray<FInterfaceRegistryUClassOptions> UClassOptions;
	};

	FMetasoundFrontendInterface ConvertParameterToFrontendInterface(const Audio::FParameterInterface& InInterface);

	void RegisterInterfaces();
	void RegisterInterface(Audio::FParameterInterfacePtr Interface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, FInterfaceRegistryOptions&& InOptions);
	void RegisterInterface(const FMetasoundFrontendInterface& InInterface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, FInterfaceRegistryOptions&& InOptions);
	void RegisterInterfaceForSingleClass(const UClass& InClass, Audio::FParameterInterfacePtr Interface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, bool bInIsDefault = false, bool bInEditorCanAddOrRemove = false, FName InRouterName = IDataReference::RouterName);
	void RegisterInterfaceForSingleClass(const UClass& InClass, const FMetasoundFrontendInterface& InInterface, TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform, bool bInIsDefault = false, bool bInEditorCanAddOrRemove = false, FName InRouterName = IDataReference::RouterName);
} // namespace Metasound::Engine
