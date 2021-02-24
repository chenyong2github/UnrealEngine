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
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "MetasoundUObjectRegistry.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "PropertyRestriction.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "MetasoundEditor"

static int32 ShowLiteralMetasoundInputsInEditorCVar = 0;
FAutoConsoleVariableRef CVarShowLiteralMetasoundInputsInEditor(
	TEXT("au.Debug.Editor.Metasounds.ShowLiteralInputs"),
	ShowLiteralMetasoundInputsInEditorCVar,
	TEXT("Show literal inputs in the Metasound Editor.\n")
	TEXT("0: Disabled (default), !0: Enabled"),
	ECVF_Default);

namespace Metasound
{
	namespace Editor
	{
		/* Enums to use when grouping the blueprint members in the list panel. The order here will determine the order in the list */
		enum class ENodeSection : uint8
		{
			NONE = 0,
			INPUTS,
			OUTPUTS
		};

		FName BuildChildPath(const FString& InBasePath, FName InPropertyName)
		{
			return FName(InBasePath + TEXT(".") + InPropertyName.ToString());
		}

		FName BuildChildPath(const FName& InBasePath, FName InPropertyName)
		{
			return FName(InBasePath.ToString() + TEXT(".") + InPropertyName.ToString());
		}

		TSet<FString> GetLiteralInputs(IDetailLayoutBuilder& InDetailLayout)
		{
			TSet<FString> LiteralInputs;
			TArray<TWeakObjectPtr<UObject>> Objects;
			InDetailLayout.GetObjectsBeingCustomized(Objects);

			if (Objects.IsEmpty() || !Objects[0].IsValid())
			{
				return LiteralInputs;
			}

			UObject* Metasound = Objects[0].Get();
			if (FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound))
			{
				Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
				TArray<Frontend::FNodeHandle> InputNodes = GraphHandle->GetInputNodes();
				for (Frontend::FNodeHandle& NodeHandle : InputNodes)
				{
					if (NodeHandle->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
					{
						LiteralInputs.Add(NodeHandle->GetNodeName());
					}
				}
			}

			return LiteralInputs;
		}

		template <typename T>
		void BuildIOFixedArray(IDetailLayoutBuilder& InDetailLayout, FName InCategoryName, FName InPropertyName)
		{
			const bool bIsInput = InCategoryName == "Inputs";

			IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory(InCategoryName);
			TSharedPtr<IPropertyHandle> ParentProperty = InDetailLayout.GetProperty(InPropertyName);
			TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentProperty->AsArray();

			TSet<FString> LiteralInputs;
			if (bIsInput && !ShowLiteralMetasoundInputsInEditorCVar)
			{
				LiteralInputs = GetLiteralInputs(InDetailLayout);
			}

			uint32 NumElements = 0;
			ArrayHandle->GetNumElements(NumElements);
			for (int32 i = 0; i < static_cast<int32>(NumElements); ++i)
			{
				TSharedRef<IPropertyHandle> ArrayItemHandle = ArrayHandle->GetElement(i);

				const FName TypeNamePropertyName = GET_MEMBER_NAME_CHECKED(T, TypeName);
				const FName NamePropertyName = GET_MEMBER_NAME_CHECKED(T, Name);
				const FName ToolTipPropertyName = GET_MEMBER_NAME_CHECKED(FMetasoundFrontendVertexMetadata, Description);
				const FName DisplayNamePropertyName = GET_MEMBER_NAME_CHECKED(FMetasoundFrontendVertexMetadata, DisplayName);

				TSharedPtr<IPropertyHandle> TypeProperty = ArrayItemHandle->GetChildHandle(TypeNamePropertyName);
				TSharedPtr<IPropertyHandle> NameProperty = ArrayItemHandle->GetChildHandle(NamePropertyName);

				TSharedPtr<IPropertyHandle> ToolTipProperty = ArrayItemHandle->GetChildHandle(ToolTipPropertyName, true /* bRecurse */);
				TSharedPtr<IPropertyHandle> DisplayNameProperty = ArrayItemHandle->GetChildHandle(DisplayNamePropertyName, true /* bRecurse */);

				FString Name;
				const bool bNameFound = NameProperty->GetValue(Name) == FPropertyAccess::Success;

				// Hide literal members
				if (LiteralInputs.Contains(Name))
				{
					continue;
				}

				CategoryBuilder.AddCustomRow(ParentProperty->GetPropertyDisplayName())
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(TAttribute<FText>::Create([i, DisplayNameProperty]()
					{
						FText DisplayName;
						DisplayNameProperty->GetValue(DisplayName);
						return DisplayName;
					}))
					.ToolTipText(TAttribute<FText>::Create([ToolTipProperty]()
					{
						FText ToolTip;
						ToolTipProperty->GetValue(ToolTip);
						return ToolTip;
					}))
				];
			}

			FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda([DetailLayout = &InDetailLayout]()
			{
				DetailLayout->ForceRefreshDetails();
			});
			ArrayHandle->SetOnNumElementsChanged(RefreshDelegate);
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

			// General Category
			IDetailCategoryBuilder& GeneralCategoryBuilder = DetailLayout.EditCategory("General");

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

			// Input/Output Categories

			// If editing multiple metasound objects, all should be the same type, so safe to just check first in array for
			// required inputs/outputs
			TArray<TWeakObjectPtr<UObject>> Objects;
			DetailLayout.GetObjectsBeingCustomized(Objects);

			const FName InterfacePropertyPath = BuildChildPath(GetMetadataRootClassPath(), GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClass, Interface));
			const FName InputsPropertyPath = BuildChildPath(InterfacePropertyPath, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassInterface, Inputs));
			const FName OutputsPropertyPath = BuildChildPath(InterfacePropertyPath, GET_MEMBER_NAME_CHECKED(FMetasoundFrontendClassInterface, Outputs));

			// TODO: Move Fixed Arrays to GraphActionMenu for selection, ordering, and
			// eventually ability to create input nodes independent of registered inputs.
// 			SAssignNew(GraphActionMenu, SGraphActionMenu, false)
// 				.OnGetFilterText(this, &FMetasoundDetailCustomization::GetFilterText)
// 				.OnCreateWidgetForAction(this, &FMetasoundDetailCustomization::OnCreateWidgetForAction)
// 				.OnCollectAllActions(this, &FMetasoundDetailCustomization::CollectAllActions)
// 				.OnCollectStaticSections(this, &FMetasoundDetailCustomization::CollectStaticSections)
// 				.OnActionDragged(this, &FMetasoundDetailCustomization::OnActionDragged)
// 				.OnActionSelected(this, &FMetasoundDetailCustomization::OnGlobalActionSelected)
// 				.OnActionDoubleClicked(this, &FMetasoundDetailCustomization::OnActionDoubleClicked)
// 				.OnContextMenuOpening(this, &FMetasoundDetailCustomization::OnContextMenuOpening)
// 				.OnCategoryTextCommitted(this, &FMetasoundDetailCustomization::OnCategoryNameCommitted)
// 				.OnCanRenameSelectedAction(this, &FMetasoundDetailCustomization::CanRequestRenameOnActionNode)
// 				.OnGetSectionTitle(this, &FMetasoundDetailCustomization::OnGetSectionTitle)
// 				.OnGetSectionWidget(this, &FMetasoundDetailCustomization::OnGetSectionWidget)
// 				.OnActionMatchesName(this, &FMetasoundDetailCustomization::HandleActionMatchesName)
// 				.AlphaSortItems(false)
// 				.UseSectionStyling(true);
			
			BuildIOFixedArray<FMetasoundFrontendClassInput>(DetailLayout, "Inputs", InputsPropertyPath);
			BuildIOFixedArray<FMetasoundFrontendClassOutput>(DetailLayout, "Outputs", OutputsPropertyPath);

			// Hack to hide parent structs for nested metadata properties
			DetailLayout.HideCategory("CustomView");

			// Hack to hide categories brought in from UMetasoundSource inherited from USoundBase
			DetailLayout.HideCategory("Analysis");
			DetailLayout.HideCategory("Attenuation");
			DetailLayout.HideCategory("Curves");
			DetailLayout.HideCategory("Debug");
			DetailLayout.HideCategory("Developer");
			DetailLayout.HideCategory("Effects");
			DetailLayout.HideCategory("File Path");
			DetailLayout.HideCategory("Format");
			DetailLayout.HideCategory("Info");
			DetailLayout.HideCategory("Loading");
			DetailLayout.HideCategory("Modulation");
			DetailLayout.HideCategory("Playback");
			DetailLayout.HideCategory("Sound");
			DetailLayout.HideCategory("SoundWave");
			DetailLayout.HideCategory("Subtitles");
			DetailLayout.HideCategory("Voice Management");
		}

		FText FMetasoundDetailCustomization::OnGetSectionTitle(int32 InSectionID)
		{
			FText SeperatorTitle;
			/* Setup an appropriate name for the section for this node */
			switch (static_cast<ENodeSection>(InSectionID))
			{
				case ENodeSection::INPUTS:
				{
					SeperatorTitle = LOCTEXT("Inputs_Title", "Inputs");
				}
				break;

				case ENodeSection::OUTPUTS:
				{
					SeperatorTitle = LOCTEXT("Outputs_Title", "Outputs");
				}
				break;

				default:
				{
					SeperatorTitle = LOCTEXT("Missing_Title", "UNSET");
				}
				break;
			}

			return SeperatorTitle;
		}
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
