// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMBlueprintViewBinding.h"

#include "Blueprint/WidgetTree.h"
#include "Bindings/MVVMConversionFunctionHelper.h"
#include "K2Node_CallFunction.h"
#include "MVVMBlueprintView.h"
#include "MVVMWidgetBlueprintExtension_View.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MVVMBlueprintViewBinding)

#define LOCTEXT_NAMESPACE "MVVMBlueprintViewBinding"

FName FMVVMBlueprintViewBinding::GetFName() const
{
	return *BindingId.ToString(EGuidFormats::DigitsWithHyphensLower);
}

namespace UE::MVVM::Private
{
	FText GetDisplayNameForField(const FMVVMConstFieldVariant& Field)
	{
		if (!Field.IsEmpty())
		{
			if (Field.IsProperty())
			{
				return Field.GetProperty()->GetDisplayNameText();
			}
			if (Field.IsFunction())
			{
				return Field.GetFunction()->GetDisplayNameText();
			}
		}
		return LOCTEXT("None", "<None>");
	}

	FString GetDisplayNameForWidget(const UWidgetBlueprint* WidgetBlueprint, const FName& WidgetName)
	{
		if (UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree)
		{
			if (UWidget* FoundWidget = WidgetTree->FindWidget(WidgetName))
			{
				return FoundWidget->GetDisplayLabel().IsEmpty() ? WidgetName.ToString() : FoundWidget->GetDisplayLabel();
			}
		}

		return WidgetName.IsNone() ? TEXT("<none>") : WidgetName.ToString();
	}

	void AppendViewModelPathString(const UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* BlueprintView, const FMVVMBlueprintPropertyPath& ViewModelPath, FStringBuilderBase& PathBuilder, FStringBuilderBase& FunctionKeywordsBuilder, bool bUseDisplayName = false, bool bAppendFunctionKeywords = false)
	{
		if (ViewModelPath.IsEmpty())
		{
			PathBuilder << TEXT("<none>");
			return;
		}

		const FMVVMBlueprintViewModelContext* ViewModel = BlueprintView != nullptr ? BlueprintView->FindViewModel(ViewModelPath.GetViewModelId()) : nullptr;
		if (ViewModel)
		{
			PathBuilder << ViewModel->GetDisplayName().ToString();
		}
		else if (ViewModelPath.IsFromWidget())
		{
			if (bUseDisplayName)
			{
				PathBuilder << GetDisplayNameForWidget(WidgetBlueprint, ViewModelPath.GetWidgetName());
			}
			else
			{
				PathBuilder << ViewModelPath.GetWidgetName();
			}
		}
		else
		{
			PathBuilder << TEXT("<none>");
		}

		TArray<UE::MVVM::FMVVMConstFieldVariant> Fields = ViewModelPath.GetFields(WidgetBlueprint->SkeletonGeneratedClass);
		for (const UE::MVVM::FMVVMConstFieldVariant& Field : Fields)
		{
			PathBuilder << TEXT(".");
			if (bUseDisplayName)
			{
				PathBuilder << GetDisplayNameForField(Field).ToString();
			}
			else
			{
				PathBuilder << Field.GetName().ToString();
			}

			if (Field.IsFunction() && bAppendFunctionKeywords)
			{
				FString FunctionKeywords = Field.GetFunction()->GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
				if (!FunctionKeywords.IsEmpty())
				{
					FunctionKeywordsBuilder << TEXT(".");
					FunctionKeywordsBuilder << FunctionKeywords;
				}
			}
		}
	}
	
	void AppendWidgetPathString(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintPropertyPath& WidgetPath, FStringBuilderBase& PathBuilder, FStringBuilderBase& FunctionKeywordsBuilder, bool bUseDisplayName = false, bool bAppendFunctionKeywords = false)
	{
		if (WidgetBlueprint == nullptr)
		{
			PathBuilder << TEXT("<none>");
			return;
		}

		if (WidgetPath.GetWidgetName().IsNone() && !bUseDisplayName)
		{
			PathBuilder << TEXT("<none>");
		}
		else if (WidgetBlueprint->GetFName() == WidgetPath.GetWidgetName())
		{
			if (bUseDisplayName)
			{
				PathBuilder << TEXT("self");
			}
			else
			{
				PathBuilder << WidgetBlueprint->GetName();
			}
		}
		else
		{
			if (bUseDisplayName)
			{
				PathBuilder << GetDisplayNameForWidget(WidgetBlueprint, WidgetPath.GetWidgetName());
			}
			else
			{
				PathBuilder << WidgetPath.GetWidgetName();
			}
		}

		TArray<UE::MVVM::FMVVMConstFieldVariant> Fields = WidgetPath.GetFields(WidgetBlueprint->SkeletonGeneratedClass);
		for (const UE::MVVM::FMVVMConstFieldVariant& Field : Fields)
		{
			PathBuilder << TEXT(".");
			if (bUseDisplayName)
			{
				PathBuilder << GetDisplayNameForField(Field).ToString();
			}
			else
			{
				PathBuilder << Field.GetName().ToString();
			}

			if (Field.IsFunction() && bAppendFunctionKeywords)
			{
				FString FunctionKeywords = Field.GetFunction()->GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
				if (!FunctionKeywords.IsEmpty())
				{
					FunctionKeywordsBuilder << TEXT(".");
					FunctionKeywordsBuilder << FunctionKeywords;
				}
			}
		}
	}

	FString GetBindingPathName(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintViewBinding& Binding, bool bIsSource, FStringBuilderBase& FunctionKeywordsBuilder, bool bUseDisplayName = false, bool bAppendFunctionKeywords = false)
	{
		UMVVMWidgetBlueprintExtension_View* ExtensionView = UMVVMWidgetBlueprintExtension_View::GetExtension<UMVVMWidgetBlueprintExtension_View>(WidgetBlueprint);
		UMVVMBlueprintView* BlueprintView = ExtensionView->GetBlueprintView();

		TStringBuilder<256> NameBuilder;

		if (UEdGraph* Graph = ConversionFunctionHelper::GetGraph(WidgetBlueprint, Binding, bIsSource))
		{
			if (const UK2Node_CallFunction* CallFunctionNode = ConversionFunctionHelper::GetFunctionNode(Graph))
			{
				if (bUseDisplayName)
				{
					if (UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
					{
						NameBuilder << TargetFunction->GetDisplayNameText().ToString();
					}
					else
					{
						NameBuilder << CallFunctionNode->GetFunctionName();
					}
				}
				else
				{
					NameBuilder << CallFunctionNode->GetFunctionName();
				}

				if (bAppendFunctionKeywords)
				{
					if (UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
					{
						FString FunctionKeywords = TargetFunction->GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
						if (!FunctionKeywords.IsEmpty())
						{
							FunctionKeywordsBuilder << TEXT(".");
							FunctionKeywordsBuilder << FunctionKeywords;
						}
					}
				}

				NameBuilder << TEXT("(");

				bool bFirst = true;

				for (const UEdGraphPin* Pin : CallFunctionNode->Pins)
				{
					if (Pin->PinName == UEdGraphSchema_K2::PN_Self || 
						Pin->Direction != EGPD_Input)
					{
						continue;
					}

					if (!bFirst)
					{
						NameBuilder << TEXT(", ");
					}

					FMVVMBlueprintPropertyPath ArgumentPath = ConversionFunctionHelper::GetPropertyPathForArgument(WidgetBlueprint, CallFunctionNode, Pin->GetFName(), true);
					if (!ArgumentPath.IsEmpty())
					{
						if (ArgumentPath.IsFromViewModel())
						{
							AppendViewModelPathString(WidgetBlueprint, BlueprintView, ArgumentPath, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);
						}
						else
						{
							AppendWidgetPathString(WidgetBlueprint, ArgumentPath, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);
						}
					}
					else
					{
						NameBuilder << Pin->GetDefaultAsString();
					}

					bFirst = false;
				}

				NameBuilder << TEXT(")");
			}
			else
			{
				NameBuilder << TEXT("<error>");
			}
		}
		else
		{
			const FMVVMBlueprintPropertyPath Path = bIsSource ? Binding.SourcePath : Binding.DestinationPath;
			const FMemberReference FunctionReference = bIsSource ? Binding.Conversion.SourceToDestinationFunction : Binding.Conversion.DestinationToSourceFunction;
			if (!FunctionReference.GetMemberName().IsNone())
			{
				if (const UFunction* Function = FunctionReference.ResolveMember<UFunction>(WidgetBlueprint->SkeletonGeneratedClass))
				{
					NameBuilder << Function->GetName();

					if (bAppendFunctionKeywords)
					{
						FString FunctionKeywords = Function->GetMetaData(FBlueprintMetadata::MD_FunctionKeywords);
						if (!FunctionKeywords.IsEmpty())
						{
							FunctionKeywordsBuilder << TEXT(".");
							FunctionKeywordsBuilder << FunctionKeywords;
						}
					}

					NameBuilder << TEXT("(");

					if (Path.IsFromViewModel())
					{
						AppendViewModelPathString(WidgetBlueprint, BlueprintView, Path, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);

					}
					else
					{
						AppendWidgetPathString(WidgetBlueprint, Path, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);
					}

					NameBuilder << TEXT(")");
				}
				else
				{
					NameBuilder << FunctionReference.GetMemberName();
					NameBuilder << TEXT("()");
				}
			}
			else
			{
				if (!Path.IsEmpty())
				{
					if (Path.IsFromViewModel())
					{
						AppendViewModelPathString(WidgetBlueprint, BlueprintView, Path, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);

					}
					else
					{
						AppendWidgetPathString(WidgetBlueprint, Path, NameBuilder, FunctionKeywordsBuilder, bUseDisplayName, bAppendFunctionKeywords);
					}
				}
			} 

		}

		FString Name = NameBuilder.ToString();
		if (Name.IsEmpty())
		{
			Name = TEXT("<none>");
		}

		return Name;
	}
}

FString FMVVMBlueprintViewBinding::GetDisplayNameString(const UWidgetBlueprint* WidgetBlueprint, bool bUseDisplayName) const
{
	check(WidgetBlueprint);

	TStringBuilder<256> NameBuilder;
	TStringBuilder<2> FunctionKeywordsBuilder; // This is only passed to GetBindingPathName but never used in this function.

	NameBuilder << UE::MVVM::Private::GetBindingPathName(WidgetBlueprint, *this, false, FunctionKeywordsBuilder, bUseDisplayName);

	if (BindingType == EMVVMBindingMode::TwoWay)
	{
		NameBuilder << TEXT(" <-> ");
	}
	else if (UE::MVVM::IsForwardBinding(BindingType))
	{
		NameBuilder << TEXT(" <- ");
	}
	else if (UE::MVVM::IsBackwardBinding(BindingType))
	{
		NameBuilder << TEXT(" -> ");
	}
	else
	{
		NameBuilder << TEXT(" ??? "); // shouldn't happen
	}

	NameBuilder << UE::MVVM::Private::GetBindingPathName(WidgetBlueprint, *this, true, FunctionKeywordsBuilder, bUseDisplayName);

	return NameBuilder.ToString();
}

FString FMVVMBlueprintViewBinding::GetSearchableString(const UWidgetBlueprint* WidgetBlueprint) const
{
	check(WidgetBlueprint);

	FString SearchString;

	// Get the binding string with variable names.
	SearchString.Append(GetDisplayNameString(WidgetBlueprint, false));

	// Remove the extra formatting that we don't need for search.
	// We will include the formatted string as well in the second call to GetBindingPathName.
	SearchString.ReplaceInline(TEXT(" "), TEXT(""));
	SearchString.ReplaceInline(TEXT(")"), TEXT(""));
	SearchString.ReplaceInline(TEXT("("), TEXT("."));
	SearchString.ReplaceInline(TEXT(","), TEXT("."));

	SearchString.Append(TEXT("."));

	// Get the binding string with display names.
	SearchString.Append(GetDisplayNameString(WidgetBlueprint, true));

	// Create the function keywords string.
	TStringBuilder<128> FunctionKeywordsBuilder;
	UE::MVVM::Private::GetBindingPathName(WidgetBlueprint, *this, false, FunctionKeywordsBuilder, false, true);
	UE::MVVM::Private::GetBindingPathName(WidgetBlueprint, *this, true, FunctionKeywordsBuilder, false, true);

	SearchString.Append(FunctionKeywordsBuilder);

	return SearchString;
}

#undef LOCTEXT_NAMESPACE