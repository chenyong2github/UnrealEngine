// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetTypeActions.h"

#include "ContentBrowserModule.h"
#include "ContentBrowserMenuContexts.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "IContentBrowserSingleton.h"
#include "ObjectEditorUtils.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundSource.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFactory.h"
#include "MetasoundUObjectRegistry.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"


#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace AssetTypeActionsPrivate
		{
			static const FText PresetLabel = LOCTEXT("MetaSound_CreatePreset", "Create MetaSound Preset");
			static const FText PresetToolTip = LOCTEXT("MetaSound_CreatePresetToolTip", "Creates a MetaSound Preset using the selected MetaSound's root graph as a reference.");
			static const FText SourcePresetLabel = LOCTEXT("MetaSoundSource_CreatePreset", "Create MetaSound Source Preset");
			static const FText SourcePresetToolTip = LOCTEXT("MetaSoundSource_CreatePresetToolTip", "Creates a MetaSoundSource Preset using the selected MetaSound's root graph as a reference.");

			template <typename TClass, typename TFactory>
			void ExecuteCreatePreset(const FToolMenuContext& MenuContext)
			{
				UContentBrowserAssetContextMenuContext* Context = MenuContext.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context || Context->SelectedObjects.IsEmpty())
				{
					return;
				}

				FString PackagePath;
				FString AssetName;

				TWeakObjectPtr<UObject> ReferencedMetaSound = Context->SelectedObjects[0];
				if (!ReferencedMetaSound.IsValid())
				{
					return;
				}

				FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
				AssetToolsModule.Get().CreateUniqueAssetName(ReferencedMetaSound->GetOutermost()->GetName(), TEXT("_Preset"), PackagePath, AssetName);

				TFactory* Factory = NewObject<TFactory>();
				check(Factory);

				Factory->ReferencedMetaSoundObject = ReferencedMetaSound.Get();

				FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowserModule.Get().CreateNewAsset(AssetName, FPackageName::GetLongPackagePath(PackagePath), TClass::StaticClass(), Factory);
			}

			template <typename TPresetClass, typename TFactory, typename TReferenceClass = TPresetClass>
			void RegisterPresetAction(const TAttribute<FText>& InLabel, const TAttribute<FText>& InToolTip)
			{
				if (!UToolMenus::IsToolMenuUIEnabled())
				{
					return;
				}

				FString ClassName = TReferenceClass::StaticClass()->GetName();
				const FString MenuName = TEXT("ContentBrowser.AssetContextMenu.") + ClassName;
				UToolMenu* Menu = UToolMenus::Get()->ExtendMenu(*MenuName);
				if (!ensure(Menu))
				{
					return;
				}

				FString PresetClassName = TPresetClass::StaticClass()->GetName();
				const FString EntryName = FString::Printf(TEXT("%sTo%s_Preset"), *PresetClassName, *ClassName);
				FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
				Section.AddDynamicEntry(*EntryName, FNewToolMenuSectionDelegate::CreateLambda([InPresetClassName = MoveTemp(PresetClassName), Label = InLabel, ToolTip = InToolTip](FToolMenuSection& InSection)
				{
					UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
					if (!Context || Context->SelectedObjects.IsEmpty())
					{
						return;
					}

					TWeakObjectPtr<UObject> MetaSoundObject = Context->SelectedObjects[0];
					if (!MetaSoundObject.IsValid())
					{
						return;
					}

					// TODO: Make class icons for MetaSound types. For now just use SoundCue.
					//const FString IconName = TEXT("ClassIcon.") + InPresetClassName;
					const FString IconName = TEXT("ClassIcon.SoundCue");

					const FSlateIcon Icon = FSlateIcon(FEditorStyle::GetStyleSetName(), *IconName);
					const FToolMenuExecuteAction UIExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteCreatePreset<TPresetClass, TFactory>);

					const FString PresetEntryName = InPresetClassName + TEXT("_CreatePreset");
					InSection.AddMenuEntry(*PresetEntryName, Label, ToolTip, Icon, UIExecuteAction);
				}));
			}
		} // namespace AssetTypeActionsPrivate

		UClass* FAssetTypeActions_MetaSound::GetSupportedClass() const
		{
			return UMetaSound::StaticClass();
		}

		void FAssetTypeActions_MetaSound::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
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

		void FAssetTypeActions_MetaSound::RegisterMenuActions()
		{
			using namespace AssetTypeActionsPrivate;
			RegisterPresetAction<UMetaSound, UMetaSoundFactory>(PresetLabel, PresetToolTip);
		}

		const TArray<FText>& FAssetTypeActions_MetaSound::GetSubMenus() const
		{
			if (GetDefault<UMetasoundEditorSettings>()->bPinMetaSoundInAssetMenu)
			{
				static const TArray<FText> SubMenus;
				return SubMenus;
			}
			
			static const TArray<FText> SubMenus
			{
				LOCTEXT("AssetSoundMetaSoundsSubMenu", "MetaSounds"),
			};

			return SubMenus;
		}

		UClass* FAssetTypeActions_MetaSoundSource::GetSupportedClass() const
		{
			return UMetaSoundSource::StaticClass();
		}

		void FAssetTypeActions_MetaSoundSource::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
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

		void FAssetTypeActions_MetaSoundSource::RegisterMenuActions()
		{
			using namespace AssetTypeActionsPrivate;
			RegisterPresetAction<UMetaSoundSource, UMetaSoundSourceFactory>(SourcePresetLabel, SourcePresetToolTip);

			// This is currently disabled because of the requirement that interfaces of a preset and its referenced
			// asset must match.  TODO: A feature of interfaces/preset transform is required for this action to work
			// that introduces the concept of an interface "interface" (i.e. if the interface one graph subscribes to
			// or contains all of the necessary inputs/outputs of another), so that the preset can "implement" the
			// referenced graphs "interface."
// 			RegisterPresetAction<UMetaSound, UMetaSoundFactory, UMetaSoundSource>(PresetLabel, PresetToolTip);
		}

		const TArray<FText>& FAssetTypeActions_MetaSoundSource::GetSubMenus() const
		{
			if (GetDefault<UMetasoundEditorSettings>()->bPinMetaSoundSourceInAssetMenu)
			{
				static const TArray<FText> SubMenus;
				return SubMenus;
			}

			static const TArray<FText> SubMenus
			{
				LOCTEXT("AssetSoundMetaSoundSourceSubMenu", "MetaSounds"),
			};

			return SubMenus;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE //MetaSoundEditor
