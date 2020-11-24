// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDetailCustomization.h"

#include "Containers/Set.h"
#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundFrontend.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "SlateCore/Public/Styling/SlateColor.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
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

		template <typename T>
		void BuildIOFixedArray(IDetailLayoutBuilder& InDetailLayout, FName CategoryName, FName InPropertyName, const TSet<FString>& RequiredValues, bool bIsInput)
		{
			IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory(CategoryName);
			TSharedPtr<IPropertyHandle> ParentProperty = InDetailLayout.GetProperty(InPropertyName);
			TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentProperty->AsArray();

			uint32 NumElements = 0;
			ArrayHandle->GetNumElements(NumElements);
			for (int32 i = 0; i < static_cast<int32>(NumElements); ++i)
			{
				TMap<FName, TSharedPtr<IPropertyHandle>> ChildProperties;
				TSharedRef<IPropertyHandle> ArrayItemHandle = ArrayHandle->GetElement(i);
				uint32 NumChildren = 0;
				ArrayItemHandle->GetNumChildren(NumChildren);
				for (int32 j = 0; j < static_cast<int32>(NumChildren); ++j)
				{
					TSharedPtr<IPropertyHandle> PropertyHandle = ArrayItemHandle->GetChildHandle(j);
					FProperty* Property = PropertyHandle->GetProperty();
					if (ensureAlways(Property))
					{
						const FName PropertyName = Property->GetFName();
						ChildProperties.Add(PropertyName, PropertyHandle);
					}
				}

				TSharedPtr<IPropertyHandle> TypeProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(T, TypeName));
				TSharedPtr<IPropertyHandle> ToolTipProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(T, ToolTip));
				TSharedPtr<IPropertyHandle> DisplayNameProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(T, DisplayName));
				TSharedPtr<IPropertyHandle> NameProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(T, Name));

				FString Name;
				const bool bNameFound = NameProperty->GetValue(Name) == FPropertyAccess::Success;
				const bool bIsRequired = bNameFound && RequiredValues.Contains(Name);

				CategoryBuilder.AddCustomRow(ParentProperty->GetPropertyDisplayName()).NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(TAttribute<FText>::Create([i, bIsRequired, DisplayNameProperty, TypeProperty]()
					{
						FName TypeName;
						TypeProperty->GetValue(TypeName);
						FString TypeNameString = TypeName.ToString();

						// Remove namespace info to keep concise
						TypeNameString.RightChopInline(TypeNameString.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);

						FText DisplayName;
						DisplayNameProperty->GetValue(DisplayName);

						if (bIsRequired)
						{
							return FText::Format(LOCTEXT("Metasound_FixedIOArrayRequiredEntry_Format", "{0}. {1} ({2}, Required)"), FText::AsNumber(i + 1), DisplayName, FText::FromString(TypeNameString));
						}
						else
						{
							return FText::Format(LOCTEXT("Metasound_FixedIOArray_Format", "{0}. {1} ({2})"), FText::AsNumber(i + 1), DisplayName, FText::FromString(TypeNameString));
						}
					}))
					.ToolTipText(TAttribute<FText>::Create([ToolTipProperty]()
					{
						FText ToolTip;
						ToolTipProperty->GetValue(ToolTip);
						return ToolTip;
					}))
				];

				if (!bIsRequired)
				{
					CategoryBuilder.AddProperty(DisplayNameProperty);
					CategoryBuilder.AddProperty(ToolTipProperty);
				}

				if (bIsInput)
				{
					TSharedPtr<IPropertyHandle> LiteralValueProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(FMetasoundInputDescription, LiteralValue));
					CategoryBuilder.AddProperty(LiteralValueProperty);
				}
			}

			FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda([DetailLayout = &InDetailLayout]()
			{
				DetailLayout->ForceRefreshDetails();
			});
			ArrayHandle->SetOnNumElementsChanged(RefreshDelegate);
		};

		FMetasoundDetailCustomization::FMetasoundDetailCustomization(FName InDocumentPropertyName)
			: IDetailCustomization()
			, DocumentPropertyName(InDocumentPropertyName)
		{
		}

		FName FMetasoundDetailCustomization::GetMetadataRootClassPath() const
		{
			return Metasound::Editor::BuildChildPath(DocumentPropertyName, GET_MEMBER_NAME_CHECKED(FMetasoundDocument, RootClass));
		}

		FName FMetasoundDetailCustomization::GetMetadataPropertyPath() const
		{
			const FName RootClass = FName(GetMetadataRootClassPath());
			return Metasound::Editor::BuildChildPath(RootClass, GET_MEMBER_NAME_CHECKED(FMetasoundClassDescription, Metadata));
		}

		void FMetasoundDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
		{
			using namespace Metasound::Editor;

			// General Category
			IDetailCategoryBuilder& GeneralCategoryBuilder = DetailLayout.EditCategory("General");

			TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
			DetailLayout.GetObjectsBeingCustomized(ObjectsCustomized);

			const FName AuthorPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassMetadata, AuthorName));
			const FName DescPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassMetadata, MetasoundDescription));
			const FName NodeTypePropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassMetadata, NodeType));
			const FName MajorVersionPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassMetadata, MajorVersion));
			const FName MinorVersionPropertyPath = BuildChildPath(GetMetadataPropertyPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassMetadata, MinorVersion));

			TSharedPtr<IPropertyHandle> AuthorHandle = DetailLayout.GetProperty(AuthorPropertyPath);
			TSharedPtr<IPropertyHandle> DescHandle = DetailLayout.GetProperty(DescPropertyPath);
			TSharedPtr<IPropertyHandle> NodeTypeHandle = DetailLayout.GetProperty(NodeTypePropertyPath);
			TSharedPtr<IPropertyHandle> MajorVersionHandle = DetailLayout.GetProperty(MajorVersionPropertyPath);
			TSharedPtr<IPropertyHandle> MinorVersionHandle = DetailLayout.GetProperty(MinorVersionPropertyPath);

			GeneralCategoryBuilder.AddProperty(NodeTypeHandle);
			GeneralCategoryBuilder.AddProperty(AuthorHandle);
			GeneralCategoryBuilder.AddProperty(DescHandle);
			GeneralCategoryBuilder.AddProperty(MajorVersionHandle);
			GeneralCategoryBuilder.AddProperty(MinorVersionHandle);

			// Input/Output Categories

			// If editing multiple metasound objects, all should be the same type, so safe to just check first in array for
			// required inputs/outputs
			TArray<TWeakObjectPtr<UObject>> Objects;
			TSet<FString> RequiredInputs;
			TSet<FString> RequiredOutputs;
			DetailLayout.GetObjectsBeingCustomized(Objects);
			if (Objects.Num() > 0)
			{
				FMetasoundAssetBase* MetasoundAsset = Frontend::GetObjectAsAssetBase(Objects[0].Get());
				check(MetasoundAsset);

				Frontend::FGraphHandle GraphHandle = MetasoundAsset->GetRootGraphHandle();
				for (const FMetasoundInputDescription& Desc : GraphHandle.GetRequiredInputs())
				{
					RequiredInputs.Add(Desc.Name);
				}
				for (const FMetasoundOutputDescription& Desc : GraphHandle.GetRequiredOutputs())
				{
					RequiredOutputs.Add(Desc.Name);
				}
			}

			const FName InputsPropertyPath = BuildChildPath(GetMetadataRootClassPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassDescription, Inputs));
			const FName OutputsPropertyPath = BuildChildPath(GetMetadataRootClassPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassDescription, Outputs));

			BuildIOFixedArray<FMetasoundInputDescription>(DetailLayout, "Inputs", InputsPropertyPath, RequiredInputs, true /* bIsInput */);
			BuildIOFixedArray<FMetasoundOutputDescription>(DetailLayout, "Outputs", OutputsPropertyPath, RequiredOutputs, false /* bIsInput */);

			// Hack to hide parent structs for nested metadata properties
			DetailLayout.HideCategory("CustomView");

			// Hack to hide categories brought in from UMetasoundSource inherited from USoundBase
			DetailLayout.HideCategory("Analysis");
			DetailLayout.HideCategory("Attenuation");
			DetailLayout.HideCategory("Curves");
			DetailLayout.HideCategory("Debug");
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
	} // namespace Editor
} // namespace Metasound
#undef LOCTEXT_NAMESPACE
