// Copyright Epic Games, Inc. All Rights Reserved.

#include "FoundationActorDetails.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "ScopedTransaction.h"

#include "Engine/World.h"
#include "Foundation/FoundationActor.h"

#define LOCTEXT_NAMESPACE "FFoundationActorDetails"

namespace FoundationActorDetailsCallbacks
{
	static bool IsEditCommitButtonEnabled(TWeakObjectPtr<AFoundationActor> FoundationActorPtr)
	{
		if (AFoundationActor* FoundationActor = FoundationActorPtr.Get())
		{
			return FoundationActor->CanEdit() || FoundationActor->CanCommit();
		}

		return false;
	}

	static FText GetEditCommitButtonText(TWeakObjectPtr<AFoundationActor> FoundationActorPtr)
	{
		if (AFoundationActor* FoundationActor = FoundationActorPtr.Get())
		{
			if (FoundationActor->CanCommit())
			{
				return LOCTEXT("CommitChanges", "Commit Changes");
			}
		}
		
		return LOCTEXT("Edit", "Edit");
	}

	static FText GetEditCommitReasonText(TWeakObjectPtr<AFoundationActor> FoundationActorPtr)
	{
		FText Reason;
		if (AFoundationActor* FoundationActor = FoundationActorPtr.Get())
		{
			if (!FoundationActor->IsEditing())
			{
				FoundationActor->CanEdit(&Reason);
				return Reason;
			}

			FoundationActor->CanCommit(&Reason);
		}
		return Reason;
	}

	static EVisibility GetEditCommitReasonVisibility(TWeakObjectPtr<AFoundationActor> FoundationActorPtr)
	{
		if (AFoundationActor* FoundationActor = FoundationActorPtr.Get())
		{
			return IsEditCommitButtonEnabled(FoundationActor) ? EVisibility::Collapsed : EVisibility::Visible;
		}

		return EVisibility::Collapsed;
	}

	static FReply OnEditCommitButtonClicked(TWeakObjectPtr<AFoundationActor> FoundationActorPtr)
	{
		if (AFoundationActor* FoundationActor = FoundationActorPtr.Get())
		{
			if (FoundationActor->CanCommit())
			{
				FoundationActor->Commit();
			}
			else if (FoundationActor->CanEdit())
			{
				FoundationActor->Edit();
			}
		}
		return FReply::Handled();
	}
}

FFoundationActorDetails::FFoundationActorDetails()
{
}

TSharedRef<IDetailCustomization> FFoundationActorDetails::MakeInstance()
{
	return MakeShareable(new FFoundationActorDetails);
}

void FFoundationActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> EditingObjects;
	DetailBuilder.GetObjectsBeingCustomized(EditingObjects);

	if (EditingObjects.Num() > 1)
	{
		return;
	}

	TWeakObjectPtr<AFoundationActor> EditingObject = Cast<AFoundationActor>(EditingObjects[0].Get());
	UWorld* World = EditingObject->GetWorld();

	if (!World)
	{
		return;
	}

	IDetailCategoryBuilder& FoundationEditingCategory = DetailBuilder.EditCategory("Foundation Editing", FText::GetEmpty(), ECategoryPriority::Transform);

	FoundationEditingCategory.AddCustomRow(FText::GetEmpty())
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNew(SMultiLineEditableTextBox)
				.Visibility_Static(&FoundationActorDetailsCallbacks::GetEditCommitReasonVisibility, EditingObject)
				.Font(DetailBuilder.GetDetailFontBold())
				.BackgroundColor(TAttribute<FSlateColor>::Create(TAttribute<FSlateColor>::FGetter::CreateLambda([]() { return FEditorStyle::GetColor("ErrorReporting.WarningBackgroundColor"); })))
				.Text_Static(&FoundationActorDetailsCallbacks::GetEditCommitReasonText, EditingObject)
				.AutoWrapText(true)
				.IsReadOnly(true)
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNew(SButton)
				.IsEnabled_Static(&FoundationActorDetailsCallbacks::IsEditCommitButtonEnabled, EditingObject)
				.Text_Static(&FoundationActorDetailsCallbacks::GetEditCommitButtonText, EditingObject)
				.HAlign(HAlign_Center)
				.OnClicked_Static(&FoundationActorDetailsCallbacks::OnEditCommitButtonClicked, EditingObject)
			]
		]
	];
}



#undef LOCTEXT_NAMESPACE