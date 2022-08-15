// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetTypeActions.h"

#include "Components/AudioComponent.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "IContentBrowserSingleton.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundSource.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFactory.h"
#include "MetasoundUObjectRegistry.h"
#include "ObjectEditorUtils.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "Widgets/Input/SButton.h"
#include "Styling/AppStyle.h"


#define LOCTEXT_NAMESPACE "MetaSoundEditor"

namespace Metasound
{
	namespace Editor
	{
		namespace AssetTypeActionsPrivate
		{
			static const FText PresetLabel = LOCTEXT("MetaSoundPatch_CreatePreset", "Create MetaSound Patch Preset");
			static const FText PresetToolTip = LOCTEXT("MetaSoundPatch_CreatePresetToolTip", "Creates a MetaSoundPatch Preset using the selected MetaSound's root graph as a reference.");
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

					const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), *IconName);
					const FToolMenuExecuteAction UIExecuteAction = FToolMenuExecuteAction::CreateStatic(&ExecuteCreatePreset<TPresetClass, TFactory>);

					const FString PresetEntryName = InPresetClassName + TEXT("_CreatePreset");
					InSection.AddMenuEntry(*PresetEntryName, Label, ToolTip, Icon, UIExecuteAction);
				}));
			}

			bool IsPlaying(const FSoftObjectPath& InSourcePath)
			{
				check(GEditor);
				if (const UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent())
				{
					if (PreviewComponent->IsPlaying())
					{
						if (const USoundBase* Sound = PreviewComponent->Sound)
						{
							const FName SoundName = *Sound->GetPathName();
							return SoundName == InSourcePath.GetAssetPathName();
						}
					}
				}

				return false;
			}

			void PlaySound(UMetaSoundSource& InSource)
			{
				// If editor is open, call into it to play to start all visualization requirements therein
				// specific to auditioning MetaSounds (ex. priming audio bus used for volume metering, playtime
				// widget, etc.)
				TSharedPtr<FEditor> Editor = FGraphBuilder::GetEditorForMetasound(InSource);
				if (Editor.IsValid())
				{
					Editor->Play();
					return;
				}

				check(GEditor);
				FGraphBuilder::RegisterGraphWithFrontend(InSource);
				GEditor->PlayPreviewSound(&InSource);
			}

			void StopSound(const UMetaSoundSource& InSource)
			{
				// If editor is open, call into it to play to start all visualization requirements therein
				// specific to auditioning MetaSounds (ex. priming audio bus used for volume metering, playtime
				// widget, etc.)
				TSharedPtr<FEditor> Editor = FGraphBuilder::GetEditorForMetasound(InSource);
				if (Editor.IsValid())
				{
					Editor->Stop();
					return;
				}

				check(GEditor);
				if (UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent())
				{
					PreviewComponent->Stop();
				}
			}
		} // namespace AssetTypeActionsPrivate

		UClass* FAssetTypeActions_MetaSoundPatch::GetSupportedClass() const
		{
			return UMetaSoundPatch::StaticClass();
		}

		FColor FAssetTypeActions_MetaSoundPatch::GetTypeColor() const
		{
			if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
			{
				return MetasoundStyle->GetColor("MetaSoundPatch.Color").ToFColorSRGB();
			}

			return FColor::White;
		}

		void FAssetTypeActions_MetaSoundPatch::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> ToolkitHost)
		{
			const EToolkitMode::Type Mode = ToolkitHost.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

			for (UObject* Object : InObjects)
			{
				if (UMetaSoundPatch* Metasound = Cast<UMetaSoundPatch>(Object))
				{
					TSharedRef<FEditor> NewEditor = MakeShared<FEditor>();
					NewEditor->InitMetasoundEditor(Mode, ToolkitHost, Metasound);
				}
			}
		}

		void FAssetTypeActions_MetaSoundPatch::RegisterMenuActions()
		{
			using namespace AssetTypeActionsPrivate;
			RegisterPresetAction<UMetaSoundPatch, UMetaSoundFactory>(PresetLabel, PresetToolTip);
		}

		const TArray<FText>& FAssetTypeActions_MetaSoundPatch::GetSubMenus() const
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

		FColor FAssetTypeActions_MetaSoundSource::GetTypeColor() const
		{
			if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
			{
				return MetasoundStyle->GetColor("MetaSoundSource.Color").ToFColorSRGB();
			}

			return FColor::White;
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
// 			RegisterPresetAction<UMetaSoundPatch, UMetaSoundFactory, UMetaSoundSource>(PresetLabel, PresetToolTip);
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

		TSharedPtr<SWidget> FAssetTypeActions_MetaSoundSource::GetThumbnailOverlay(const FAssetData& AssetData) const
		{
			auto OnGetDisplayBrushLambda = [Path = AssetData.ToSoftObjectPath()]()
			{
				using namespace AssetTypeActionsPrivate;

				if (IsPlaying(Path))
				{
					return FAppStyle::GetBrush("MediaAsset.AssetActions.Stop.Large");
				}

				return FAppStyle::GetBrush("MediaAsset.AssetActions.Play.Large");
			};

			auto OnClickedLambda = [Path = AssetData.ToSoftObjectPath()]()
			{
				using namespace AssetTypeActionsPrivate;
				// Load and play sound
				if (UMetaSoundSource* MetaSoundSource = Cast<UMetaSoundSource>(Path.TryLoad()))
				{
					if (IsPlaying(Path))
					{
						StopSound(*MetaSoundSource);
					}
					else
					{
						PlaySound(*MetaSoundSource);
					}
				}

				return FReply::Handled();
			};

			auto OnToolTipTextLambda = [Path = AssetData.ToSoftObjectPath()]()
			{
				using namespace AssetTypeActionsPrivate;

				FText Format;
				if (IsPlaying(Path))
				{
					Format = LOCTEXT("StopPreviewMetaSoundFromIconToolTip", "Stop Previewing {0}");
				}
				else
				{
					Format = LOCTEXT("PreviewMetaSoundFromIconToolTip_Editor", "Preview {0}");
				}

				FName TypeName = UMetaSoundSource::StaticClass()->GetFName();
				return FText::Format(Format, FText::FromName(TypeName));
			};

			TSharedPtr<SBox> Box;
			SAssignNew(Box, SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(FMargin(2));

			auto OnGetVisibilityLambda = [Box, Path = AssetData.ToSoftObjectPath()]()
			{
				using namespace AssetTypeActionsPrivate;

				if (Box.IsValid() && (Box->IsHovered() || IsPlaying(Path)))
				{
					return EVisibility::Visible;
				}

				return EVisibility::Hidden;
			};

			TSharedPtr<SButton> Widget;
			SAssignNew(Widget, SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ToolTipText_Lambda(OnToolTipTextLambda)
				.Cursor(EMouseCursor::Default) // The outer widget can specify a DragHand cursor, so overriden here
				.ForegroundColor(FSlateColor::UseForeground())
				.IsFocusable(false)
				.OnClicked_Lambda(OnClickedLambda)
				.Visibility_Lambda(OnGetVisibilityLambda)
				[
					SNew(SImage)
					.Image_Lambda(OnGetDisplayBrushLambda)
				];

			Box->SetContent(Widget.ToSharedRef());
			Box->SetVisibility(EVisibility::Visible);

			return Box;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE //MetaSoundEditor
