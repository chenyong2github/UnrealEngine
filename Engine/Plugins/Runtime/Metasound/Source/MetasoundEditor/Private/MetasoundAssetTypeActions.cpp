// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetTypeActions.h"

#include "Metasound.h"
#include "MetasoundEditor.h"


#define LOCTEXT_NAMESPACE "AssetTypeActions"

namespace MetasoundEditorUtils
{
	static const TArray<FText> AssetTypeActionSubMenu
	{
		LOCTEXT("AssetMetasoundSubMenu", "Metasound")
	};
} // namespace MetasoundEditorUtils

UClass* FAssetTypeActions_Metasound::GetSupportedClass() const
{
	return UMetasound::StaticClass();
}

const TArray<FText>& FAssetTypeActions_Metasound::GetSubMenus() const
{
	return MetasoundEditorUtils::AssetTypeActionSubMenu;
}

void FAssetTypeActions_Metasound::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
{
	const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (UObject* Object : InObjects)
	{
		if (UMetasound* Metasound = Cast<UMetasound>(Object))
		{
			TSharedRef<FMetasoundEditor> NewEditor = MakeShared<FMetasoundEditor>();
			NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
		}
	}
}
#undef LOCTEXT_NAMESPACE
