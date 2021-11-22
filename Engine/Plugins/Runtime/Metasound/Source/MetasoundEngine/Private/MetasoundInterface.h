// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Algo/Transform.h"
#include "AudioParameterInterface.h"
#include "IAudioGeneratorInterfaceRegistry.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundLog.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "Templates/UniquePtr.h"
#include "UObject/Class.h"
#include "UObject/NoExportTypes.h"


namespace Metasound
{
	namespace Engine
	{
		void RegisterExternalInterfaces();

		struct FInterfaceRegistryOptions
		{
			bool bIsDefault = false;
			FName InputSystemName;
			FName UClassName;
		};

		// Entry for registered interface.
		class FInterfaceRegistryEntry : public Frontend::IInterfaceRegistryEntry
		{
		public:
			FInterfaceRegistryEntry(
				const FMetasoundFrontendInterface& InInterface,
				TUniquePtr<Frontend::IDocumentTransform>&& InUpdateTransform,
				FInterfaceRegistryOptions&& InOptions
			)
				: Interface(InInterface)
				, UpdateTransform(MoveTemp(InUpdateTransform))
				, Options(MoveTemp(InOptions))
			{
			}

			virtual bool UClassIsSupported(FName InUClassName) const override
			{
				if (Options.UClassName.IsNone())
				{
					return true;
				}

				// TODO: Support child asset class types.
				return Options.UClassName == InUClassName;
			}

			virtual bool IsDefault() const override
			{
				return Options.bIsDefault;
			}

			virtual FName GetRouterName() const override
			{
				return Options.InputSystemName;
			}

			virtual const FMetasoundFrontendInterface& GetInterface() const override
			{
				return Interface;
			}

			virtual bool UpdateRootGraphInterface(Frontend::FDocumentHandle InDocument) const override
			{
				if (UpdateTransform.IsValid())
				{
					return UpdateTransform->Transform(InDocument);
				}
				return false;
			}

			private:
				FMetasoundFrontendInterface Interface;
				TUniquePtr<Frontend::IDocumentTransform> UpdateTransform;
				FInterfaceRegistryOptions Options;
		};

		template <typename UClassType>
		void RegisterInterface(const FMetasoundFrontendInterface& InInterface, TUniquePtr<Frontend::IDocumentTransform>&& UpdateTransform, bool bInIsDefault, FName InRouterName)
		{
			FInterfaceRegistryOptions Options
			{
				bInIsDefault,
				InRouterName,
				UClassType::StaticClass()->GetFName()
			};

			IMetasoundUObjectRegistry::Get().RegisterUClassInterface(MakeUnique<TMetasoundUObjectRegistryEntry<UClassType>>(InInterface.Version));
			Frontend::IInterfaceRegistry::Get().RegisterInterface(MakeUnique<FInterfaceRegistryEntry>(InInterface, MoveTemp(UpdateTransform), MoveTemp(Options)));
		}
	} // namespace Engine
} // namespace Metasound
