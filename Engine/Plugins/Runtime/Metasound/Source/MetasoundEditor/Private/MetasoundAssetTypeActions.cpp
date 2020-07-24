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
		static const TArray<FText> AssetTypeActionSubMenu
		{
			LOCTEXT("AssetMetasoundSubMenu", "Metasound")
		};

		UClass* FAssetTypeActions_Metasound::GetSupportedClass() const
		{
			return UMetasound::StaticClass();
		}

		const TArray<FText>& FAssetTypeActions_Metasound::GetSubMenus() const
		{
			return AssetTypeActionSubMenu;
		}

		void FAssetTypeActions_Metasound::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
		{
			const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

			for (UObject* Object : InObjects)
			{
				if (UMetasound* Metasound = Cast<UMetasound>(Object))
				{
					TSharedRef<FEditor> NewEditor = MakeShared<FEditor>();
					NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
				}
			}
		}

		UClass* FAssetTypeActions_MetasoundSource::GetSupportedClass() const
		{
			return UMetasoundSource::StaticClass();
		}

		void FAssetTypeActions_MetasoundSource::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
		{
			const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

			for (UObject* Object : InObjects)
			{
				if (UMetasoundSource* Metasound = Cast<UMetasoundSource>(Object))
				{
					TSharedRef<FEditor> NewEditor = MakeShared<FEditor>();
					NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
				}
			}
		}

		const TArray<FText>& FAssetTypeActions_MetasoundSource::GetSubMenus() const
		{
			return AssetTypeActionSubMenu;
		}
	} // namespace Editor
} // namespace Metasound

#undef LOCTEXT_NAMESPACE

