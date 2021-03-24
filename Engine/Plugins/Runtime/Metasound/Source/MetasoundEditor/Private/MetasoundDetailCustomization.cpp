// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDetailCustomization.h"

#include "Containers/Set.h"
#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailGroup.h"
#include "Input/Events.h"
#include "MetasoundAssetBase.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "SGraphPalette.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"


namespace Metasound
{
	namespace Editor
	{
		FName BuildChildPath(const FString& InBasePath, FName InPropertyName)
		{
			return FName(InBasePath + TEXT(".") + InPropertyName.ToString());
		}

		FName BuildChildPath(const FName& InBasePath, FName InPropertyName)
		{
			return FName(InBasePath.ToString() + TEXT(".") + InPropertyName.ToString());
		}

		FMetasoundDetailCustomization::FMetasoundDetailCustomization(FName InDocumentPropertyName)
			: IDetailCustomization()
			, DocumentPropertyName(InDocumentPropertyName)
		{
		}

		FName FMetasoundDetailCustomization::GetMetadataRootClassPath() const
		{
			return Metasound::Editor::BuildChildPath(DocumentPropertyName, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendDocument, RootGraph));
		}

		FName FMetasoundDetailCustomization::GetMetadataPropertyPath() const
		{
			const FName RootClass = FName(GetMetadataRootClassPath());
			return Metasound::Editor::BuildChildPath(RootClass, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClass, Metadata));
		}

		void FMetasoundDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Metasound::Editor;

			EMetasoundActiveDetailView DetailsView = EMetasoundActiveDetailView::Metasound;
			if (const UMetasoundEditorSettings* EditorSettings = GetDefault<UMetasoundEditorSettings>())
			{
				DetailsView = EditorSettings->DetailView;
			}

			switch (DetailsView)
			{
				case EMetasoundActiveDetailView::Metasound:
				{
					IDetailCategoryBuilder& GeneralCategoryBuilder = DetailLayout.EditCategory("Metasound");
					const FName AuthorPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassMetadata, Author));
					const FName DescPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassMetadata, Description));
					const FName VersionPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassMetadata, Version));
					const FName MajorVersionPropertyPath = BuildChildPath(VersionPropertyPath, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendVersionNumber, Major));
					const FName MinorVersionPropertyPath = BuildChildPath(VersionPropertyPath, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendVersionNumber, Minor));

					TSharedPtr<IPropertyHandle> AuthorHandle = DetailLayout.GetProperty(AuthorPropertyPath);
					TSharedPtr<IPropertyHandle> DescHandle = DetailLayout.GetProperty(DescPropertyPath);
					TSharedPtr<IPropertyHandle> MajorVersionHandle = DetailLayout.GetProperty(MajorVersionPropertyPath);
					TSharedPtr<IPropertyHandle> MinorVersionHandle = DetailLayout.GetProperty(MinorVersionPropertyPath);

					GeneralCategoryBuilder.AddProperty(AuthorHandle);
					GeneralCategoryBuilder.AddProperty(DescHandle);
					GeneralCategoryBuilder.AddProperty(MajorVersionHandle);
					GeneralCategoryBuilder.AddProperty(MinorVersionHandle);

					// Hack to hide categories brought in from UMetasoundSource inherited from USoundBase
					DetailLayout.HideCategory("Analysis");
					DetailLayout.HideCategory("Attenuation");
					DetailLayout.HideCategory("Debug");
					DetailLayout.HideCategory("Effects");
					DetailLayout.HideCategory("Loading");
					DetailLayout.HideCategory("Modulation");
					DetailLayout.HideCategory("Sound");
					DetailLayout.HideCategory("SoundWave");
					DetailLayout.HideCategory("Voice Management");
				}
				break;

				case EMetasoundActiveDetailView::General:
				default:
					DetailLayout.HideCategory("Metasound");

					const bool bShouldBeInitiallyCollapsed = true;
					DetailLayout.EditCategory("Analysis").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Attenuation").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Debug").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Effects").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Modulation").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Sound").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("SoundWave").InitiallyCollapsed(bShouldBeInitiallyCollapsed);
					DetailLayout.EditCategory("Voice Management").InitiallyCollapsed(bShouldBeInitiallyCollapsed);

					const bool bRestore = false;
					DetailLayout.EditCategory("Analysis").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Attenuation").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Debug").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Effects").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Modulation").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Sound").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("SoundWave").RestoreExpansionState(bRestore);
					DetailLayout.EditCategory("Voice Management").RestoreExpansionState(bRestore);

					break;
			}

			// Hack to hide parent structs for nested metadata properties
			DetailLayout.HideCategory("CustomView");

			DetailLayout.HideCategory("Curves");
			DetailLayout.HideCategory("Developer");
			DetailLayout.HideCategory("File Path");
			DetailLayout.HideCategory("Format");
			DetailLayout.HideCategory("Info");
			DetailLayout.HideCategory("Loading");
			DetailLayout.HideCategory("Playback");
			DetailLayout.HideCategory("Subtitles");
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
