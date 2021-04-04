// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetTypeActions.h"

#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditor.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace Metasound
{
	namespace Editor
	{
		UClass* FAssetTypeActions_Metasound::GetSupportedClass() const
		{
			return UMetaSound::StaticClass();
		}

		void FAssetTypeActions_Metasound::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
		{
			const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

			for (UObject* Object : InObjects)
			{
				if (UMetaSound* Metasound = Cast<UMetaSound>(Object))
				{
					TSharedRef<FEditor> NewEditor = MakeShared<FEditor>();
					NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
				}
			}
		}

		UClass* FAssetTypeActions_MetasoundSource::GetSupportedClass() const
		{
			return UMetaSoundSource::StaticClass();
		}

		void FAssetTypeActions_MetasoundSource::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
		{
			const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

			for (UObject* Object : InObjects)
			{
				if (UMetaSoundSource* Metasound = Cast<UMetaSoundSource>(Object))
				{
					TSharedRef<FEditor> NewEditor = MakeShared<FEditor>();
					NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
				}
			}
		}
	} // namespace Editor
} // namespace Metasound

#undef LOCTEXT_NAMESPACE

