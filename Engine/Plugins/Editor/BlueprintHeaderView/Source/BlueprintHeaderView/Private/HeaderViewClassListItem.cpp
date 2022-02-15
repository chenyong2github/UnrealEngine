// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeaderViewClassListItem.h"
#include "Engine/Blueprint.h"
#include "String/LineEndings.h"

FHeaderViewListItemPtr FHeaderViewClassListItem::Create(TWeakObjectPtr<UBlueprint> InBlueprint)
{
	return MakeShareable(new FHeaderViewClassListItem(InBlueprint));
}

FString FHeaderViewClassListItem::GetConditionalUClassSpecifiers(const UBlueprint* Blueprint) const
{
	TStringBuilder<256> AdditionalSpecifiers;

	if (Blueprint->bGenerateConstClass)
	{
		AdditionalSpecifiers.Append(TEXT(", Const"));
	}

	if (Blueprint->bGenerateAbstractClass)
	{
		AdditionalSpecifiers.Append(TEXT(", Abstract"));
	}

	if (!Blueprint->BlueprintCategory.IsEmpty())
	{
		AdditionalSpecifiers.Append(FString::Printf(TEXT(", Category=\"%s\""), *Blueprint->BlueprintCategory));
	}

	if (!Blueprint->HideCategories.IsEmpty())
	{
		AdditionalSpecifiers.Append(TEXT(", HideCategories=("));

		for (int32 HideCategoryIdx = 0; HideCategoryIdx < Blueprint->HideCategories.Num(); ++HideCategoryIdx)
		{
			if (HideCategoryIdx == 0)
			{
				AdditionalSpecifiers.Append(FString::Printf(TEXT("\"%s\""), *Blueprint->HideCategories[HideCategoryIdx]));
			}
			else
			{
				AdditionalSpecifiers.Append(FString::Printf(TEXT(", \"%s\""), *Blueprint->HideCategories[HideCategoryIdx]));
			}
		}

		AdditionalSpecifiers.Append(TEXT(")"));
	}

	if (!Blueprint->BlueprintDisplayName.IsEmpty() || !Blueprint->BlueprintNamespace.IsEmpty())
	{
		AdditionalSpecifiers.Append(TEXT(", meta=("));

		int32 SpecifierCount = 0;

		if (!Blueprint->BlueprintDisplayName.IsEmpty())
		{
			AdditionalSpecifiers.Append(FString::Printf(TEXT("DisplayName=\"%s\""), *Blueprint->BlueprintDisplayName));
			++SpecifierCount;
		}

		if (!Blueprint->BlueprintNamespace.IsEmpty())
		{
			if (SpecifierCount > 0)
			{
				AdditionalSpecifiers.Append(FString::Printf(TEXT(", Namespace=\"%s\""), *Blueprint->BlueprintNamespace));
			}
			else
			{
				AdditionalSpecifiers.Append(FString::Printf(TEXT("Namespace=\"%s\""), *Blueprint->BlueprintNamespace));
			}
		}

		AdditionalSpecifiers.Append(TEXT(")"));
	}

	return AdditionalSpecifiers.ToString();
}

FHeaderViewClassListItem::FHeaderViewClassListItem(TWeakObjectPtr<UBlueprint> InBlueprint)
{
	if (UBlueprint* Blueprint = InBlueprint.Get())
	{
		// Avoid lots of reallocations
		RawItemString.Reserve(512);
		RichTextString.Reserve(512);

		// Format comment
		{
			FString Comment = Blueprint->BlueprintDescription.IsEmpty() ? TEXT("Please add a class description") : Blueprint->BlueprintDescription;
			FormatCommentString(Comment, RawItemString, RichTextString);
		}

		// Add the UCLASS specifiers
		// i.e. UCLASS(Blueprintable, BlueprintType, Category=BlueprintCategory)
		{
			FString AdditionalSpecifiers = GetConditionalUClassSpecifiers(Blueprint);

			// Always add Blueprintable and BlueprintType
			RawItemString += FString::Printf(TEXT("\nUCLASS(Blueprintable, BlueprintType%s)"), *AdditionalSpecifiers);
			RichTextString += FString::Printf(TEXT("\n<%s>UCLASS</>(Blueprintable, BlueprintType%s)"), *HeaderViewSyntaxDecorators::MacroDecorator, *AdditionalSpecifiers);
		}

		// Add the class declaration line
		// i.e. class ClassName : public ParentClass
		{
			const FString BlueprintName = Blueprint->SkeletonGeneratedClass->GetPrefixCPP() + Blueprint->GetName();

			const UObject* ParentBlueprint = Blueprint->ParentClass->ClassGeneratedBy;
			FString ParentClassName = FString::Printf(TEXT("%s%s"), 
				ParentBlueprint ? TEXT("") : Blueprint->ParentClass->GetPrefixCPP(),
				ParentBlueprint ? *ParentBlueprint->GetName() : *Blueprint->ParentClass->GetAuthoredName());

			RawItemString += FString::Printf(TEXT("\nclass %s : public %s\n{\n\tGENERATED_BODY()"), *BlueprintName, *ParentClassName);
			RichTextString += FString::Printf(TEXT("\n<%s>class</> <%s>%s</> : <%s>public</> <%s>%s</>\n{\n\t<%s>GENERATED_BODY</>()"), 
				*HeaderViewSyntaxDecorators::KeywordDecorator,
				*HeaderViewSyntaxDecorators::TypenameDecorator, 
				*BlueprintName,
				*HeaderViewSyntaxDecorators::KeywordDecorator,
				*HeaderViewSyntaxDecorators::TypenameDecorator,
				*ParentClassName,
				*HeaderViewSyntaxDecorators::MacroDecorator
			);
		}

		// normalize to platform newlines
		UE::String::ToHostLineEndingsInline(RawItemString);
		UE::String::ToHostLineEndingsInline(RichTextString);
	}
}

