// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeaderViewVariableListItem.h"
#include "EdMode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "String/LineEndings.h"

FHeaderViewListItemPtr FHeaderViewVariableListItem::Create(const FBPVariableDescription& VariableDesc, const FProperty& VarProperty)
{
	return MakeShareable(new FHeaderViewVariableListItem(VariableDesc, VarProperty));
}

FHeaderViewVariableListItem::FHeaderViewVariableListItem(const FBPVariableDescription& VariableDesc, const FProperty& VarProperty)
{
	// Format comment
	{
		FString Comment = VarProperty.GetMetaData(FBlueprintMetadata::MD_Tooltip);
		if (Comment.IsEmpty())
		{
			Comment = TEXT("Please add a variable description");
		}

		FormatCommentString(Comment, RawItemString, RichTextString);
	}

	// Add a static_assert if the user needs to tell these variables how to replicate
	// i.e. static_assert(false, "You will need to add DOREPLIFETIME(ClassName, VarName) to GetLifeTimeReplicatedProps");
	if (VarProperty.HasAnyPropertyFlags(CPF_Net))
	{
		FString ClassName = GetOwningClassName(VarProperty);
		if (VariableDesc.ReplicationCondition == COND_None)
		{
			RawItemString += FString::Printf(TEXT("\nstatic_assert(false, \"You will need to add DOREPLIFETIME(%s, %s) to GetLifetimeReplicatedProps\");"), *ClassName, *VarProperty.GetAuthoredName());
			RichTextString += FString::Printf(TEXT("\n<%s>static_assert</>(<%s>false</>, \"You will need to add DOREPLIFETIME(%s, %s) to GetLifetimeReplicatedProps\");"), 
				*HeaderViewSyntaxDecorators::KeywordDecorator, 
				*HeaderViewSyntaxDecorators::KeywordDecorator, 
				*ClassName, 
				*VarProperty.GetAuthoredName());
		}
		else
		{
			FString ConditionString = StaticEnum<ELifetimeCondition>()->GetAuthoredNameStringByValue(VariableDesc.ReplicationCondition);
			RawItemString += FString::Printf(TEXT("\nstatic_assert(false, \"You will need to add DOREPLIFETIME_WITH_PARAMS(%s, %s, %s) to GetLifetimeReplicatedProps\");"), *ClassName, *VarProperty.GetAuthoredName(), *ConditionString);
			RichTextString += FString::Printf(TEXT("\n<%s>static_assert</>(<%s>false</>, \"You will need to add DOREPLIFETIME_WITH_PARAMS(%s, %s, %s) to GetLifetimeReplicatedProps\");"),
				*HeaderViewSyntaxDecorators::KeywordDecorator,
				*HeaderViewSyntaxDecorators::KeywordDecorator,
				*ClassName,
				*VarProperty.GetAuthoredName(),
				*ConditionString);
		}
	}

	// Add Deprecation message if present
	if (VarProperty.HasAnyPropertyFlags(CPF_Deprecated))
	{
		FString DeprecationMessage = VarProperty.GetMetaData(FBlueprintMetadata::MD_DeprecationMessage);
		if (DeprecationMessage.IsEmpty())
		{
			DeprecationMessage = TEXT("Please add a deprecation message.");
		}

		const FString EngineVersionString = FEngineVersion::Current().ToString(EVersionComponent::Patch);

		RawItemString += FString::Printf(TEXT("\nUE_DEPRECATED(%s, \"%s\")"), *EngineVersionString, *DeprecationMessage);
		RichTextString += FString::Printf(TEXT("\n<%s>UE_DEPRECATED</>(%s, \"%s\")"), *HeaderViewSyntaxDecorators::MacroDecorator, *EngineVersionString, *DeprecationMessage);
	}

	// Add the UPROPERTY specifiers
	// i.e. UPROPERTY(BlueprintReadWrite, Category="Variable Category")
	{
		const FString AdditionalSpecifiers = GetConditionalUPropertySpecifiers(VarProperty);

		// Always add Blueprintable and BlueprintType
		RawItemString += FString::Printf(TEXT("\nUPROPERTY(%s)"), *AdditionalSpecifiers);
		RichTextString += FString::Printf(TEXT("\n<%s>UPROPERTY</>(%s)"), *HeaderViewSyntaxDecorators::MacroDecorator, *AdditionalSpecifiers);
	}

	// Add the variable declaration line
	// i.e. Type VariableName;
	{
		const FString Typename = GetCPPTypenameForProperty(&VarProperty);
		const FString VarName = VarProperty.GetAuthoredName();

		RawItemString += FString::Printf(TEXT("\n%s %s;"), *Typename, *VarName);
		RichTextString += FString::Printf(TEXT("\n<%s>%s</> <%s>%s</>;"), 
			*HeaderViewSyntaxDecorators::TypenameDecorator,
			*Typename.Replace(TEXT("<"), TEXT("&lt;")).Replace(TEXT(">"), TEXT("&gt;")),
			*HeaderViewSyntaxDecorators::IdentifierDecorator,
			*VarName);
	}

	// indent item
	RawItemString.InsertAt(0, TEXT("\t"));
	RichTextString.InsertAt(0, TEXT("\t"));
	RawItemString.ReplaceInline(TEXT("\n"), TEXT("\n\t"));
	RichTextString.ReplaceInline(TEXT("\n"), TEXT("\n\t"));

	// normalize to platform newlines
	UE::String::ToHostLineEndingsInline(RawItemString);
	UE::String::ToHostLineEndingsInline(RichTextString);
}

FString FHeaderViewVariableListItem::GetConditionalUPropertySpecifiers(const FProperty& VarProperty) const
{
	TArray<FString> PropertySpecifiers;

	if (!VarProperty.HasMetaData(FBlueprintMetadata::MD_Private) || !VarProperty.GetBoolMetaData(FBlueprintMetadata::MD_Private))
	{
		if (VarProperty.HasAllPropertyFlags(CPF_BlueprintReadOnly))
		{
			PropertySpecifiers.Emplace(TEXT("BlueprintReadOnly"));
		}
		else
		{
			PropertySpecifiers.Emplace(TEXT("BlueprintReadWrite"));
		}
	}

	if (!VarProperty.HasAnyPropertyFlags(CPF_DisableEditOnInstance | CPF_DisableEditOnTemplate))
	{
		PropertySpecifiers.Emplace(TEXT("EditAnywhere"));
	}
	else if (VarProperty.HasAnyPropertyFlags(CPF_DisableEditOnInstance))
	{
		PropertySpecifiers.Emplace(TEXT("EditDefaultsOnly"));
	}
	else
	{
		PropertySpecifiers.Emplace(TEXT("EditInstanceOnly"));
	}

	if (VarProperty.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
	{
		PropertySpecifiers.Emplace(FString::Printf(TEXT("Category=\"%s\""), *VarProperty.GetMetaData(FBlueprintMetadata::MD_FunctionCategory)));
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_Net))
	{
		if (VarProperty.HasAnyPropertyFlags(CPF_RepNotify))
		{
			PropertySpecifiers.Emplace(FString::Printf(TEXT("ReplicatedUsing=\"OnRep_%s\""), *VarProperty.GetAuthoredName()));
		}
		else
		{
			PropertySpecifiers.Emplace(TEXT("Replicated"));
		}
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_Interp))
	{
		PropertySpecifiers.Emplace(TEXT("Interp"));
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_Config))
	{
		PropertySpecifiers.Emplace(TEXT("Config"));
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_Transient))
	{
		PropertySpecifiers.Emplace(TEXT("Transient"));
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_SaveGame))
	{
		PropertySpecifiers.Emplace(TEXT("SaveGame"));
	}

	if (VarProperty.HasAnyPropertyFlags(CPF_AdvancedDisplay))
	{
		PropertySpecifiers.Emplace(TEXT("AdvancedDisplay"));
	}

	if (VarProperty.HasMetaData(FEdMode::MD_MakeEditWidget) && VarProperty.GetBoolMetaData(FEdMode::MD_MakeEditWidget))
	{
		PropertySpecifiers.Emplace(TEXT("MakeEditWidget"));
	}

	// meta specifiers
	{
		// These metadata keys are handled explicitly elsewhere
		static const TArray<FName> IgnoreMetaData = 
		{
			FBlueprintMetadata::MD_DeprecationMessage,
			FBlueprintMetadata::MD_FunctionCategory,
			FBlueprintMetadata::MD_Tooltip,
			FBlueprintMetadata::MD_Private,
			FEdMode::MD_MakeEditWidget
		};

		TArray<FString> MetaSpecifiers;

		for (const TPair<FName, FString>& MetaPair : *VarProperty.GetMetaDataMap())
		{
			// Don't take this metadata if we handled it explicitly
			if (IgnoreMetaData.Contains(MetaPair.Key))
			{
				continue;
			}

			// Don't add the DisplayName metadata key if the display name is just the friendly name generated by the editor anyway
			if (MetaPair.Key == FBlueprintMetadata::MD_DisplayName)
			{
				if (MetaPair.Value == FName::NameToDisplayString(VarProperty.GetName(), !!CastField<FBoolProperty>(&VarProperty)))
				{
					continue;
				}
			}

			MetaSpecifiers.Emplace(FString::Printf(TEXT("%s=\"%s\""), *MetaPair.Key.ToString(), *MetaPair.Value));
		}

		if (!MetaSpecifiers.IsEmpty())
		{
			PropertySpecifiers.Emplace(FString::Printf(TEXT("meta=(%s)"), *FString::Join(MetaSpecifiers, TEXT(", "))));
		}
	}

	return FString::Join(PropertySpecifiers, TEXT(", "));
}

FString FHeaderViewVariableListItem::GetOwningClassName(const FProperty& VarProperty) const
{
	UClass* OwningClass = VarProperty.GetOwnerClass();
	if (OwningClass && OwningClass->ClassGeneratedBy)
	{
		return OwningClass->GetPrefixCPP() + OwningClass->ClassGeneratedBy->GetName();
	}

	return TEXT("");
}
