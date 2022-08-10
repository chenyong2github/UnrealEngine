// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendNodeTemplateRegistry.h"

#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundLog.h"
#include "MetasoundTrace.h"


namespace Metasound
{
	namespace Frontend
	{
		class FNodeTemplateRegistry : public INodeTemplateRegistry
		{
		public:
			static FNodeTemplateRegistry& Get();

			FNodeTemplateRegistry() = default;
			virtual ~FNodeTemplateRegistry() = default;

			virtual const INodeTemplate* FindTemplate(const FNodeRegistryKey& InKey) const override;

			void Register(TUniquePtr<INodeTemplate>&& InEntry);
			void Unregister(const FMetasoundFrontendVersion& InNodeTemplateVersion);

		private:
			TMap<FNodeRegistryKey, TUniquePtr<INodeTemplate>> Templates;
		};

		void FNodeTemplateRegistry::Register(TUniquePtr<INodeTemplate>&& InTemplate)
		{
			if (ensure(InTemplate.IsValid()))
			{
				const FMetasoundFrontendVersion& Version = InTemplate->GetVersion();
				const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(InTemplate->GetFrontendClass().Metadata);
				if (ensure(NodeRegistryKey::IsValid(Key)))
				{
					Templates.Add(Key, MoveTemp(InTemplate));
				}
			}
		}

		void FNodeTemplateRegistry::Unregister(const FMetasoundFrontendVersion& InVersion)
		{
			const FNodeRegistryKey Key = NodeRegistryKey::CreateKey(EMetasoundFrontendClassType::Template, InVersion.Name.ToString(), InVersion.Number.Major, InVersion.Number.Minor);
			if (ensure(NodeRegistryKey::IsValid(Key)))
			{
				Templates.Remove(Key);
			}
		}

		const INodeTemplate* FNodeTemplateRegistry::FindTemplate(const FNodeRegistryKey& InKey) const
		{
			if (const TUniquePtr<INodeTemplate>* TemplatePtr = Templates.Find(InKey))
			{
				return TemplatePtr->Get();
			}

			return nullptr;
		}

		INodeTemplateRegistry& INodeTemplateRegistry::Get()
		{
			static FNodeTemplateRegistry Registry;
			return Registry;
		}

		void RegisterNodeTemplate(TUniquePtr<INodeTemplate>&& InTemplate)
		{
			static_cast<FNodeTemplateRegistry&>(INodeTemplateRegistry::Get()).Register(MoveTemp(InTemplate));
		}

		void UnregisterNodeTemplate(const FMetasoundFrontendVersion& InVersion)
		{
			static_cast<FNodeTemplateRegistry&>(INodeTemplateRegistry::Get()).Unregister(InVersion);
		}
	} // namespace Frontend
} // namespace Metasound
