// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundDetailCustomization.h"

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "PropertyEditorDelegates.h"
#include "PropertyHandle.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"


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
		void BuildFixedArray(IDetailLayoutBuilder& InDetailLayout, FName CategoryName, FName InPropertyName)
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
				TSharedPtr<IPropertyHandle> NameProperty = ChildProperties.FindChecked(GET_MEMBER_NAME_CHECKED(T, DisplayName));

				FDetailWidgetRow& NameRow = CategoryBuilder.AddCustomRow(ParentProperty->GetPropertyDisplayName())
					.NameContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(TAttribute<FText>::Create([i, NameProperty, TypeProperty]()
						{
							FString TypeNameString;
							ensureAlways(TypeProperty->GetValue(TypeNameString) == FPropertyAccess::Success);

							// Remove namespace info to keep concise
							TypeNameString.RightChopInline(TypeNameString.Find(TEXT(":"), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);

							FText DisplayName;
							ensureAlways(NameProperty->GetValue(DisplayName) == FPropertyAccess::Success);

							return FText::Format(LOCTEXT("Metasound_FixedArray_Format", "{0}. {1} ({2})"), FText::AsNumber(i + 1), DisplayName, FText::FromString(TypeNameString));
						}))
						.ToolTipText(TAttribute<FText>::Create([ParentProperty, TypeProperty]()
						{
							FString TypeNameString;
							ensureAlways(TypeProperty->GetValue(TypeNameString) == FPropertyAccess::Success);

							return FText::Format(LOCTEXT("", "Type {0}\n"), FText::FromString(TypeNameString), ParentProperty->GetToolTipText());
						}))
					];

				for (const TPair<FName, TSharedPtr<IPropertyHandle>>& Pair : ChildProperties)
				{
					if (Pair.Key != GET_MEMBER_NAME_CHECKED(T, TypeName))
					{
						CategoryBuilder.AddProperty(Pair.Value);
					}
				}
			}

			FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda([DetailLayout = &InDetailLayout]()
				{
					DetailLayout->ForceRefreshDetails();
				});
			ArrayHandle->SetOnNumElementsChanged(RefreshDelegate);
		};
	}
}

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

	TSharedPtr<IPropertyHandle> AuthorHandle = DetailLayout.GetProperty(AuthorPropertyPath);
	TSharedPtr<IPropertyHandle> DescHandle = DetailLayout.GetProperty(DescPropertyPath);
	TSharedPtr<IPropertyHandle> NodeTypeHandle = DetailLayout.GetProperty(NodeTypePropertyPath);

	GeneralCategoryBuilder.AddProperty(NodeTypeHandle);
	GeneralCategoryBuilder.AddProperty(AuthorHandle);
	GeneralCategoryBuilder.AddProperty(DescHandle);

	// Input/Output Categories

	const FName InputsPropertyPath = BuildChildPath(GetMetadataRootClassPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassDescription, Inputs));
	const FName OutputsPropertyPath = BuildChildPath(GetMetadataRootClassPath(), GET_MEMBER_NAME_CHECKED(FMetasoundClassDescription, Outputs));

	BuildFixedArray<FMetasoundInputDescription>(DetailLayout, "Inputs", InputsPropertyPath);
	BuildFixedArray<FMetasoundOutputDescription>(DetailLayout, "Outputs", OutputsPropertyPath);

	// Hack to hide parent structs for nested metadata properties
	DetailLayout.HideCategory("Hidden");

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
	DetailLayout.HideCategory("Subtitles");
	DetailLayout.HideCategory("Voice Management");
}
#undef LOCTEXT_NAMESPACE