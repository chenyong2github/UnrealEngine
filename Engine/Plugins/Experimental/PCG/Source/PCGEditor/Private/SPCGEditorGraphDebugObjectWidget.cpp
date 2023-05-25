// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphDebugObjectWidget.h"

#include "PCGComponent.h"
#include "PCGEditor.h"
#include "PCGEditorGraph.h"

#include "PropertyCustomizationHelpers.h"
#include "Selection.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphDebugObjectWidget"

FPCGEditorGraphDebugObjectInstance::FPCGEditorGraphDebugObjectInstance()
	: Label(LOCTEXT("NoDebugObject", "No debug object selected"))
{
}

FPCGEditorGraphDebugObjectInstance::FPCGEditorGraphDebugObjectInstance(TWeakObjectPtr<UPCGComponent> InPCGComponent, const FPCGStack& InPCGStack)
	: PCGComponent(InPCGComponent)
	, PCGStack(InPCGStack)
{
	FString PathLabel = InPCGComponent->GetOwner()->GetActorNameOrLabel() / InPCGComponent->GetName();

	for (const FPCGStackFrame& StackFrame : PCGStack.GetStackFrames())
	{
		PathLabel /= StackFrame.Object->GetName();
	}

	Label = FText::FromString(PathLabel);
}

void SPCGEditorGraphDebugObjectWidget::Construct(const FArguments& InArgs, TSharedPtr<FPCGEditor> InPCGEditor)
{
	PCGEditorPtr = InPCGEditor;

	DebugObjects.Add(MakeShared<FPCGEditorGraphDebugObjectInstance>());

	const TSharedRef<SWidget> SetButton = PropertyCustomizationHelpers::MakeUseSelectedButton(
		FSimpleDelegate::CreateSP(this, &SPCGEditorGraphDebugObjectWidget::SetDebugObjectFromSelection_OnClicked),
		LOCTEXT("SetDebugObject", "Set debug object from Level Editor selection."),
		TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SPCGEditorGraphDebugObjectWidget::IsSetDebugObjectFromSelectionButtonEnabled))
	);

	const TSharedRef<SWidget> BrowseButton = PropertyCustomizationHelpers::MakeBrowseButton(
		FSimpleDelegate::CreateSP(this, &SPCGEditorGraphDebugObjectWidget::SelectedDebugObject_OnClicked),
		LOCTEXT("DebugSelectActor", "Select and frame the debug actor in the Level Editor."),
		TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SPCGEditorGraphDebugObjectWidget::IsSelectDebugObjectButtonEnabled))
	);

	DebugObjectsComboBox = SNew(SComboBox<TSharedPtr<FPCGEditorGraphDebugObjectInstance>>)
		.OptionsSource(&DebugObjects)
		.InitiallySelectedItem(DebugObjects[0])
		.OnComboBoxOpening(this, &SPCGEditorGraphDebugObjectWidget::OnComboBoxOpening)
		.OnGenerateWidget(this, &SPCGEditorGraphDebugObjectWidget::OnGenerateWidget)
		.OnSelectionChanged(this, &SPCGEditorGraphDebugObjectWidget::OnSelectionChanged)
		.Content()
		[
			SNew(STextBlock)
			.Text(this, &SPCGEditorGraphDebugObjectWidget::GetSelectedDebugObjectText)
		];

	this->ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			DebugObjectsComboBox.ToSharedRef()
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f)
		[
			SetButton
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f)
		[
			BrowseButton
		]
	];
}

void SPCGEditorGraphDebugObjectWidget::OnComboBoxOpening()
{
	DebugObjects.Empty();
	DebugObjectsComboBox->RefreshOptions();

	const UPCGGraph* PCGGraph = GetPCGGraph();
	if (!PCGGraph)
	{
		return;
	}

	const TSharedPtr<FPCGEditorGraphDebugObjectInstance> SelectedItem = DebugObjectsComboBox->GetSelectedItem();

	DebugObjects.Add(MakeShared<FPCGEditorGraphDebugObjectInstance>());

	if (!SelectedItem.IsValid() || !SelectedItem->GetPCGComponent().IsValid())
	{
		DebugObjectsComboBox->SetSelectedItem(DebugObjects[0]);
	}

	TArray<UObject*> PCGComponents;
	GetObjectsOfClass(UPCGComponent::StaticClass(), PCGComponents, /*bIncludeDerivedClasses=*/ true);
	for (UObject* PCGComponentObject : PCGComponents)
	{
		if (!IsValid(PCGComponentObject))
		{
			continue;
		}

		UPCGComponent* PCGComponent = Cast<UPCGComponent>(PCGComponentObject);
		if (!PCGComponent)
		{
			continue;
		}

		const AActor* Actor = PCGComponent->GetOwner();
		if (!Actor)
		{
			continue;
		}

		const UPCGGraph* PCGComponentGraph = PCGComponent->GetGraph();
		if (!PCGComponentGraph)
		{
			continue;
		}

		FPCGStackContext StackContext = FPCGStackContext::CreateStackContextFromGraph(PCGComponentGraph);

		for (const FPCGStack& Stack : StackContext.GetStacks())
		{
			const FPCGStackFrame& StackFrame = Stack.GetStackFrames().Top();
			if (const UPCGGraph* StackGraph = Cast<const UPCGGraph>(StackFrame.Object))
			{
				if (StackGraph == PCGGraph)
				{
					const TSharedPtr<FPCGEditorGraphDebugObjectInstance> DebugInstance = MakeShared<FPCGEditorGraphDebugObjectInstance>(PCGComponent, Stack);
					DebugObjects.Add(DebugInstance);

					if (SelectedItem.IsValid() && SelectedItem->GetPCGComponent() == PCGComponent && SelectedItem->GetStack() == Stack)
					{
						DebugObjectsComboBox->SetSelectedItem(DebugInstance);
					}
				}
			}
		}
	}
}

void SPCGEditorGraphDebugObjectWidget::OnSelectionChanged(TSharedPtr<FPCGEditorGraphDebugObjectInstance> NewSelection, ESelectInfo::Type SelectInfo) const
{
	if (NewSelection.IsValid())
	{
		UPCGComponent* PCGComponent = NewSelection->GetPCGComponent().Get();
		PCGEditorPtr.Pin()->SetComponentAndStackBeingInspected(PCGComponent, NewSelection->GetStack());
	}
}

TSharedRef<SWidget> SPCGEditorGraphDebugObjectWidget::OnGenerateWidget(TSharedPtr<FPCGEditorGraphDebugObjectInstance> InDebugObjectInstance) const
{
	const FText ItemText = InDebugObjectInstance->GetDebugObjectText();

	return SNew(STextBlock)
		.Text(ItemText);
}

UPCGGraph* SPCGEditorGraphDebugObjectWidget::GetPCGGraph() const
{
	if (!PCGEditorPtr.IsValid())
	{
		return nullptr;
	}

	const UPCGEditorGraph* PCGEditorGraph = PCGEditorPtr.Pin()->GetPCGEditorGraph();
	if (!PCGEditorGraph)
	{
		return nullptr;
	}

	return PCGEditorGraph->GetPCGGraph();
}

FText SPCGEditorGraphDebugObjectWidget::GetSelectedDebugObjectText() const
{
	if (const TSharedPtr<FPCGEditorGraphDebugObjectInstance> SelectedItem = DebugObjectsComboBox->GetSelectedItem())
	{
		return SelectedItem->GetDebugObjectText();
	}

	return FText::GetEmpty();
}

void SPCGEditorGraphDebugObjectWidget::SelectedDebugObject_OnClicked() const
{
	if (UPCGComponent* PCGComponent = PCGEditorPtr.Pin()->GetPCGComponentBeingInspected())
	{
		if (AActor* Actor = PCGComponent->GetOwner())
		{
			GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true, /*WarnAboutManyActors=*/false);
			GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/true, /*bSelectEvenIfHidden=*/true);
			GUnrealEd->Exec(Actor->GetWorld(), TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY"));
			GEditor->SelectComponent(PCGComponent, /*bInSelected=*/true, /*bNotify=*/true, /*bSelectEvenIfHidden=*/true);
		}
	}
}

bool SPCGEditorGraphDebugObjectWidget::IsSelectDebugObjectButtonEnabled() const
{
	return PCGEditorPtr.IsValid() && (PCGEditorPtr.Pin()->GetPCGComponentBeingInspected() != nullptr);
}

void SPCGEditorGraphDebugObjectWidget::SetDebugObjectFromSelection_OnClicked()
{
	const UPCGGraph* PCGGraph = GetPCGGraph();
	if (!PCGGraph)
	{
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!IsValid(SelectedActors))
	{
		return;
	}

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		const AActor* SelectedActor = Cast<AActor>(*It);
		if (!IsValid(SelectedActor))
		{
			continue;
		}

		UPCGComponent* PCGComponent = SelectedActor->GetComponentByClass<UPCGComponent>();
		if (!IsValid(PCGComponent))
		{
			continue;
		}

		FPCGStackContext StackContext = FPCGStackContext::CreateStackContextFromGraph(PCGComponent->GetGraph());

		for (const FPCGStack& Stack : StackContext.GetStacks())
		{
			const FPCGStackFrame& StackFrame = Stack.GetStackFrames().Top();
			if (const UPCGGraph* StackGraph = Cast<const UPCGGraph>(StackFrame.Object))
			{
				if (StackGraph == PCGGraph)
				{
					DebugObjects.Empty();
					DebugObjectsComboBox->RefreshOptions();

					const TSharedPtr<FPCGEditorGraphDebugObjectInstance> DebugInstance = MakeShared<FPCGEditorGraphDebugObjectInstance>(PCGComponent, Stack);
					DebugObjects.Add(DebugInstance);
					DebugObjectsComboBox->SetSelectedItem(DebugInstance);
					PCGEditorPtr.Pin()->SetComponentAndStackBeingInspected(PCGComponent, Stack);
					break;
				}
			}
		}
	}
}

bool SPCGEditorGraphDebugObjectWidget::IsSetDebugObjectFromSelectionButtonEnabled() const
{
	const UPCGGraph* PCGGraph = GetPCGGraph();
	if (!PCGGraph)
	{
		return false;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!IsValid(SelectedActors))
	{
		return false;
	}

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		const AActor* SelectedActor = Cast<AActor>(*It);
		if (!IsValid(SelectedActor))
		{
			continue;
		}
		
		const UPCGComponent* PCGComponent = SelectedActor->GetComponentByClass<UPCGComponent>();
		if (!IsValid(PCGComponent))
		{
			continue;
		}

		FPCGStackContext StackContext = FPCGStackContext::CreateStackContextFromGraph(PCGComponent->GetGraph());
		for (const FPCGStack& Stack : StackContext.GetStacks())
		{
			const FPCGStackFrame& StackFrame = Stack.GetStackFrames().Top();
			if (const UPCGGraph* StackGraph = Cast<const UPCGGraph>(StackFrame.Object))
			{
				if (StackGraph == PCGGraph)
				{
					return true;
				}
			}
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
