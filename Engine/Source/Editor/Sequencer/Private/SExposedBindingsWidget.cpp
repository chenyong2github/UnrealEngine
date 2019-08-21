// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SExposedBindingsWidget.h"

#include "Sequencer.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneObjectBindingIDPicker.h"

#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "ScopedTransaction.h"

#include "Algo/Sort.h"

#define LOCTEXT_NAMESPACE "SExposedBindingsWidget"


struct SExposedBindingPicker : public SCompoundWidget, public FMovieSceneObjectBindingIDPicker
{
	SLATE_BEGIN_ARGS(SExposedBindingPicker){}
	SLATE_END_ARGS()


	void Construct(const FArguments& InArgs, FName InExposedName, UMovieSceneSequence* InSequence, FMovieSceneObjectBindingID InCurrentValue)
	{
		ExposedName = InExposedName;
		Sequence = InSequence;
		CurrentValue = InCurrentValue;

		Initialize();

		ChildSlot
		[
			SNew(SComboButton)
			.ToolTipText(this, &SExposedBindingPicker::GetToolTipText)
			.OnGetMenuContent(this, &SExposedBindingPicker::GetPickerMenu)
			.ContentPadding(FMargin(4.0, 2.0))
			.ButtonContent()
			[
				GetCurrentItemWidget(SNew(STextBlock))
			]
		];
	}

private:

	virtual UMovieSceneSequence* GetSequence() const override
	{
		return Sequence;
	}

	virtual void SetCurrentValue(const FMovieSceneObjectBindingID& InBindingId) override
	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		MovieScene->Modify();
		MovieScene->RemoveExposedBinding(ExposedName, CurrentValue);
		MovieScene->ExposeBinding(ExposedName, InBindingId);

		CurrentValue = InBindingId;
	}

	virtual FMovieSceneObjectBindingID GetCurrentValue() const
	{
		return CurrentValue;
	}

	FName ExposedName;
	UMovieSceneSequence* Sequence;
	FMovieSceneObjectBindingID CurrentValue;
};

struct SExposedNameSubMenuContent : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SExposedNameSubMenuContent){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FName InExposedName, UMovieSceneSequence* InSequence)
	{
		ExposedName = InExposedName;
		Sequence = InSequence;

		Reconstruct();
	}

	void Reconstruct()
	{
		FMenuBuilder MenuBuilder(true, nullptr);

		const FMovieSceneObjectBindingIDs* IDs = Sequence->GetMovieScene()->AllBindingGroups().Find(ExposedName);
		if (IDs)
		{
			for (FMovieSceneObjectBindingID ID : IDs->IDs)
			{
				MenuBuilder.AddWidget(
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
						.OnClicked(this, &SExposedNameSubMenuContent::OnRemove, ID)
						[
							SNew(STextBlock)
							.ColorAndOpacity(FLinearColor::Red)
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
							.Text(FEditorFontGlyphs::Times)
						]
					]

					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					[
						SNew(SExposedBindingPicker, ExposedName, Sequence, ID)
					],
					FText(),
					true
				);
			}
		}

		MenuBuilder.AddWidget(
			SNew(SBox)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SExposedNameSubMenuContent::AddEmpty)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::Green)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Plus)
				]
			],
			FText(),
			true
		);

		ChildSlot
		[
			MenuBuilder.MakeWidget()
		];
	}

	FReply AddEmpty()
	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		FScopedTransaction Transaction(FText::Format(LOCTEXT("AddNewExposedBinding", "Add new binding for exposed name {0}"), FText::FromName(ExposedName)));

		MovieScene->Modify();
		MovieScene->ExposeBinding(ExposedName, FMovieSceneObjectBindingID());

		Reconstruct();

		return FReply::Handled();
	}

	FReply OnRemove(FMovieSceneObjectBindingID ID)
	{
		UMovieScene* MovieScene = Sequence->GetMovieScene();

		FScopedTransaction Transaction(FText::Format(LOCTEXT("RemoveExposedBinding", "Remove binding from exposed name {0}"), FText::FromName(ExposedName)));

		MovieScene->Modify();
		MovieScene->RemoveExposedBinding(ExposedName, ID);

		Reconstruct();
		return FReply::Handled();
	}

	FName ExposedName;
	UMovieSceneSequence* Sequence;
};

void SExposedBindingsWidget::Construct(const FArguments& InArgs, TWeakPtr<ISequencer> InWeakSequencer)
{
	WeakSequencer = InWeakSequencer;

	Reconstruct();
}

void SExposedBindingsWidget::Reconstruct()
{
	TSharedPtr<ISequencer> Sequencer  = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence   = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence  ? Sequence->GetMovieScene()                 : nullptr;

	FMenuBuilder MenuBuilder(true, nullptr);

	if (!MovieScene)
	{
		ChildSlot
		[
			SNullWidget::NullWidget
		];

		return;
	}

	TSharedRef<SVerticalBox> VerticalBox = SNew(SVerticalBox);

	TArray<FName> ExposedNames;

	const TMap<FName, FMovieSceneObjectBindingIDs>& BindingMap = MovieScene->AllBindingGroups();
	for (const TTuple<FName, FMovieSceneObjectBindingIDs>& Pair : BindingMap)
	{
		ExposedNames.Add(Pair.Key);
	}

	// Sort exposed names alphabetically
	Algo::Sort(ExposedNames, [](const FName& A, const FName& B) { return A.Compare(B) < 0; });

	for (FName ExposedName : ExposedNames)
	{
		TSharedRef<SWidget> MenuContent =
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SExposedBindingsWidget::RemoveExposedName, ExposedName)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FLinearColor::Red)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
					.Text(FEditorFontGlyphs::Times)
				]
			]

			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SExposedBindingsWidget::GetSubMenuLabel, ExposedName)
			];

		MenuBuilder.AddSubMenu(MenuContent, FNewMenuDelegate::CreateSP(this, &SExposedBindingsWidget::MakeSubMenu, ExposedName));
	}

	TSharedRef<SEditableTextBox> EditableText = SNew(SEditableTextBox)
	.OnTextCommitted(this, &SExposedBindingsWidget::OnNewTextCommitted)
	.HintText(LOCTEXT("AddNew_Hint", "Add new"));

	auto OnClicked = [this, WeakEditableText = TWeakPtr<SEditableTextBox>(EditableText)]() -> FReply
	{
		TSharedPtr<SEditableTextBox> LocalEditableText = WeakEditableText.Pin();
		if (LocalEditableText)
		{
			this->OnNewTextCommitted(LocalEditableText->GetText(), ETextCommit::OnEnter);
		}
		return FReply::Handled();
	};

	TSharedRef<SWidget> NewNameContent =
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		.Padding(FMargin(0.f, 0.f, 5.f, 0.f))
		.AutoWidth()
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.OnClicked_Lambda(OnClicked)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FLinearColor::Green)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.11"))
				.Text(FEditorFontGlyphs::Plus)
			]
		]

		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.MinDesiredWidth(100.f)
			[
				EditableText
			]
		];

	MenuBuilder.AddWidget(NewNameContent, FText(), true);

	ChildSlot
	[
		MenuBuilder.MakeWidget()
	];
}

FText SExposedBindingsWidget::GetSubMenuLabel(FName ExposedName) const
{
	TSharedPtr<ISequencer> Sequencer  = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence   = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence  ? Sequence->GetMovieScene()                 : nullptr;
	if (!MovieScene)
	{
		return FText();
	}

	return FText::Format(LOCTEXT("NamedBinding_Label", "{0} ({1} {1}|plural(one=binding,other=Bindings))")
		, FText::FromName(ExposedName)
		, FText::AsNumber(MovieScene->AllBindingGroups().FindChecked(ExposedName).IDs.Num())
	);
}

void SExposedBindingsWidget::MakeSubMenu(FMenuBuilder& MenuBuilder, FName ExposedName)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	MenuBuilder.AddWidget(SNew(SExposedNameSubMenuContent, ExposedName, Sequencer->GetFocusedMovieSceneSequence()), FText(), true);
}

void SExposedBindingsWidget::OnNewTextCommitted(const FText& InNewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter && !InNewText.IsEmpty())
	{
		FName NewName = *InNewText.ToString();
		ExposeAsName(NewName);
	}
}

void SExposedBindingsWidget::ExposeAsName(FName InNewName)
{
	TSharedPtr<ISequencer> Sequencer = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ExposeBinding_Transaction", "Expose binding as {0}"), FText::FromName(InNewName)));

	MovieScene->Modify();
	MovieScene->ExposeBinding(InNewName);

	Reconstruct();
}

FReply SExposedBindingsWidget::RemoveExposedName(FName InNameToRemove)
{
	TSharedPtr<ISequencer> Sequencer  = WeakSequencer.Pin();
	UMovieSceneSequence*   Sequence   = Sequencer ? Sequencer->GetFocusedMovieSceneSequence() : nullptr;
	UMovieScene*           MovieScene = Sequence  ? Sequence->GetMovieScene()                 : nullptr;

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ExposeBinding_Transaction", "Stop exposing binding {0}"), FText::FromName(InNameToRemove)));

	MovieScene->Modify();
	MovieScene->RemoveExposedBinding(InNameToRemove);

	Reconstruct();
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE