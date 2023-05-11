// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletonEditingToolPropertyCustomizations.h"

#include "DetailLayoutBuilder.h"
#include "Templates/UniquePtr.h"
#include "SkeletalMesh/SkeletonEditingTool.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InteractiveToolManager.h"


#define LOCTEXT_NAMESPACE "SkeletonEditingToolCustomization"

TSharedRef<IDetailCustomization> FSkeletonEditingPropertiesDetailCustomization::MakeInstance()
{
	return MakeShareable(new FSkeletonEditingPropertiesDetailCustomization);
}

void FSkeletonEditingPropertiesDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	RelativeArray.Init(true, 4);
	
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() > 1)
	{
		return;
	}

	USkeletonEditingProperties* Properties = CastChecked<USkeletonEditingProperties>(ObjectsBeingCustomized[0]);
	Tool = Properties ? Properties->ParentTool : nullptr;
	if (!Tool.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Details"));

	const TSharedRef<IPropertyHandle> NodePropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkeletonEditingProperties, Name));
	if (NodePropHandle->IsValidHandle())
	{
		TSharedRef<SWidget> ValueWidget = NodePropHandle->CreatePropertyValueWidget();
		ValueWidget->SetEnabled(TAttribute<bool>::Create([this]{ return !Tool->GetSelection().IsEmpty(); }));

		static const FText MultiValues = LOCTEXT("MultipleValues", "Multiple Values");
		
		DetailBuilder.AddPropertyToCategory(NodePropHandle)
			.CustomWidget()
			.NameContent()
			[
				NodePropHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				SNew(SEditableTextBox)
					.Font(FAppStyle::GetFontStyle( TEXT( "PropertyWindow.NormalFont" ) ))
					.SelectAllTextWhenFocused(true)
					.ClearKeyboardFocusOnCommit(false)
					.SelectAllTextOnCommit(true)
					.Text_Lambda([this]()
					{
						const TArray<FName>& Bones = Tool->GetSelection();
						if (Bones.Num() == 0)
						{
							return FText::FromName(NAME_None);
						}
						if (Bones.Num() == 1)
						{
							return FText::FromName(Bones[0]);
						}
						return MultiValues;
					})
					.OnTextCommitted_Lambda([this, NodePropHandle](const FText& NewText, ETextCommit::Type) 
					{
						if (NewText.EqualTo(MultiValues))
						{
							return;
						}
						FText CurrentText; NodePropHandle->GetValueAsFormattedText(CurrentText);
						if (!NewText.ToString().Equals(CurrentText.ToString(), ESearchCase::CaseSensitive))
						{
							NodePropHandle->SetValueFromFormattedString(NewText.ToString());
						}
					})
					.IsEnabled_Lambda([this] { return !Tool->GetSelection().IsEmpty(); })
			];
	}
	
	SAdvancedTransformInputBox<FTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FTransform>::FArguments()
		.IsEnabled(true)
		.DisplayRelativeWorld(true)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.DisplayScaleLock(false)
		.AllowEditRotationRepresentation(false);

	// relative/world
	TransformWidgetArgs
	.OnGetIsComponentRelative_Lambda( [this](ESlateTransformComponent::Type InComponent)
	{
	   return RelativeArray[InComponent];
	})
	.OnIsComponentRelativeChanged_Lambda( [this](ESlateTransformComponent::Type InComponent, bool bIsRelative)
	{
		RelativeArray[InComponent] = bIsRelative;
	});
	
	// get bones transforms
	CustomizeValueGet(TransformWidgetArgs);
		
	// set bones transforms
	CustomizeValueSet(TransformWidgetArgs);

	// copy/paste values
	CustomizeClipboard(TransformWidgetArgs);

	// enabled
	TransformWidgetArgs
	.IsEnabled_Lambda([this]()
	{
		return !Tool->GetSelection().IsEmpty();
	});
	
	SAdvancedTransformInputBox<FTransform>::ConstructGroupedTransformRows(
		CategoryBuilder, 
		LOCTEXT("ReferenceTransform", "Transform"),
		LOCTEXT("ReferenceBoneTransformTooltip", "The reference transform of the bone"), 
		TransformWidgetArgs);
}

void FSkeletonEditingPropertiesDetailCustomization::CustomizeValueGet(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs)
{
	if (!Tool.IsValid())
	{
		return;
	}
	
	auto GetNumericValue = [this](
	   const FName InBoneName,
	   ESlateTransformComponent::Type InComponent,
	   ESlateRotationRepresentation::Type InRepresentation,
	   ESlateTransformSubComponent::Type InSubComponent)
	{
		const bool bWorld = !RelativeArray[InComponent];
		return SAdvancedTransformInputBox<FTransform>::GetNumericValueFromTransform(
			  Tool->GetTransform(InBoneName, bWorld),
			  InComponent,
			  InRepresentation,
			  InSubComponent);
	};
	
	InOutArgs.OnGetNumericValue_Lambda( [this, GetNumericValue](
		ESlateTransformComponent::Type InComponent,
		ESlateRotationRepresentation::Type InRepresentation,
		ESlateTransformSubComponent::Type InSubComponent)
	{
		const TArray<FName>& Bones = Tool->GetSelection();
		if (Bones.IsEmpty())
		{
			return SAdvancedTransformInputBox<FTransform>::GetNumericValueFromTransform(
				FTransform::Identity, InComponent, InRepresentation, InSubComponent);
		}
		
		TOptional<FVector::FReal> Value = GetNumericValue(Bones[0], InComponent, InRepresentation, InSubComponent);
		if (Value)
		{
			for (int32 Index = 1; Index < Bones.Num(); Index++)
			{
				const TOptional<FVector::FReal> NextValue = GetNumericValue(Bones[Index], InComponent, InRepresentation, InSubComponent);
				if (NextValue)
				{
					if (!FMath::IsNearlyEqual(*Value, *NextValue))
					{
						return TOptional<FVector::FReal>();
					}
				}
			}
		}
		return Value;
	} );
}

void FSkeletonEditingPropertiesDetailCustomization::CustomizeValueSet(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs)
{
	if (!Tool.IsValid())
	{
		return;
	}
	
	auto PrepareNumericValueChanged = [this]( const FName InBoneName,
											   ESlateTransformComponent::Type InComponent,
											   ESlateRotationRepresentation::Type InRepresentation,
											   ESlateTransformSubComponent::Type InSubComponent,
											   FTransform::FReal InValue)
	{
		const bool bWorld = !RelativeArray[InComponent];
		const FTransform& InTransform = Tool->GetTransform(InBoneName, bWorld);
		FTransform OutTransform = InTransform;
		SAdvancedTransformInputBox<FTransform>::ApplyNumericValueChange(OutTransform, InValue, InComponent, InRepresentation, InSubComponent);
		return MakeTuple(InTransform, OutTransform);
	};

	InOutArgs.OnNumericValueChanged_Lambda([this, PrepareNumericValueChanged](
		ESlateTransformComponent::Type InComponent,
		ESlateRotationRepresentation::Type InRepresentation,
		ESlateTransformSubComponent::Type InSubComponent,
		FVector::FReal InValue)
	{
		const TArray<FName>& Bones = Tool->GetSelection();
		TArray<FName> BonesToMove; BonesToMove.Reserve(Bones.Num());
		TArray<FTransform> UpdatedTransforms; UpdatedTransforms.Reserve(Bones.Num());
		
		FTransform CurrentTransform, UpdatedTransform;
		for (const FName& BoneName: Bones)
		{
			Tie(CurrentTransform, UpdatedTransform) =
				PrepareNumericValueChanged(BoneName, InComponent, InRepresentation, InSubComponent, InValue);
			if (!UpdatedTransform.Equals(CurrentTransform))
			{
				BonesToMove.Add(BoneName);
				UpdatedTransforms.Add(UpdatedTransform);
			}
		}

		if (!BonesToMove.IsEmpty())
		{
			if (!ActiveChange.IsValid())
			{
				ActiveChange = MakeUnique<SkeletonEditingTool::FRefSkeletonChange>(Tool.Get());
			}
		
			const bool bWorld = !RelativeArray[InComponent];
			Tool->SetTransforms(BonesToMove, UpdatedTransforms, bWorld);
		}
	})
	.OnNumericValueCommitted_Lambda([this, PrepareNumericValueChanged](
		ESlateTransformComponent::Type InComponent,
		ESlateRotationRepresentation::Type InRepresentation,
		ESlateTransformSubComponent::Type InSubComponent,
		FVector::FReal InValue,
		ETextCommit::Type InCommitType)
	{
		const TArray<FName>& Bones = Tool->GetSelection();

		TArray<FName> BonesToMove; BonesToMove.Reserve(Bones.Num());
		TArray<FTransform> UpdatedTransforms; UpdatedTransforms.Reserve(Bones.Num());
		
		FTransform CurrentTransform, UpdatedTransform;
		for (const FName& BoneName: Bones)
		{
			Tie(CurrentTransform, UpdatedTransform) =
				PrepareNumericValueChanged(BoneName, InComponent, InRepresentation, InSubComponent, InValue);
			if (!UpdatedTransform.Equals(CurrentTransform))
			{
				BonesToMove.Add(BoneName);
				UpdatedTransforms.Add(UpdatedTransform);
			}
		}

		if (!BonesToMove.IsEmpty())
		{
			if (!ActiveChange.IsValid())
			{
				ActiveChange = MakeUnique<SkeletonEditingTool::FRefSkeletonChange>(Tool.Get());
			}

			const bool bWorld = !RelativeArray[InComponent];
			Tool->SetTransforms(BonesToMove, UpdatedTransforms, bWorld);
		}

		if (ActiveChange.IsValid())
		{
			// send transaction
			if (UInteractiveToolManager* ToolManager = Tool->GetToolManager())
			{
				ActiveChange->StoreSkeleton(Tool.Get());

				static const FText TransactionDesc = LOCTEXT("ChangeNumericValue", "Change Numeric Value");
				ToolManager->BeginUndoTransaction(TransactionDesc);
				ToolManager->EmitObjectChange(Tool.Get(), MoveTemp(ActiveChange), TransactionDesc);
				ToolManager->EndUndoTransaction();
			}
		
			ActiveChange.Reset();
		}
	});
}

namespace FSkeletonEditingToolClipboardLocals
{
	
template<typename DataType>
void GetContentFromData(const DataType& InData, FString& Content)
{
	TBaseStructure<DataType>::Get()->ExportText(Content, &InData, &InData, nullptr, PPF_None, nullptr);
}

class FSkeletonEditingToolBoneErrorPipe : public FOutputDevice
{
public:
	int32 NumErrors;

	FSkeletonEditingToolBoneErrorPipe()
		: FOutputDevice()
		, NumErrors(0)
	{}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		NumErrors++;
	}
};

template<typename DataType>
bool GetDataFromContent(const FString& Content, DataType& OutData)
{
	FSkeletonEditingToolBoneErrorPipe ErrorPipe;
	static UScriptStruct* DataStruct = TBaseStructure<DataType>::Get();
	DataStruct->ImportText(*Content, &OutData, nullptr, PPF_None, &ErrorPipe, DataStruct->GetName(), true);
	return (ErrorPipe.NumErrors == 0);
}
	
}

void FSkeletonEditingPropertiesDetailCustomization::CustomizeClipboard(SAdvancedTransformInputBox<FTransform>::FArguments& InOutArgs)
{
	if (!Tool.IsValid())
	{
		return;
	}
	
	auto OnCopyToClipboard = [this](const FName InBoneName, ESlateTransformComponent::Type InComponent)
	{
		using namespace FSkeletonEditingToolClipboardLocals;

		const bool bWorld = !RelativeArray[InComponent];
		const FTransform& Xfo = Tool->GetTransform(InBoneName, bWorld);

		FString Content;
		switch(InComponent)
		{
		case ESlateTransformComponent::Location:
			{
				GetContentFromData(Xfo.GetLocation(), Content);
				break;
			}
		case ESlateTransformComponent::Rotation:
			{
				GetContentFromData(Xfo.Rotator(), Content);
				break;
			}
		case ESlateTransformComponent::Scale:
			{
				GetContentFromData(Xfo.GetScale3D(), Content);
				break;
			}
		case ESlateTransformComponent::Max:
		default:
			{
				GetContentFromData(Xfo, Content);
				break;
			}
		}

		if (!Content.IsEmpty())
		{
			FPlatformApplicationMisc::ClipboardCopy(*Content);
		}
	};

	auto PreparePasteFromClipboard = [this](const FName InBoneName, ESlateTransformComponent::Type InComponent)
	{
		using namespace FSkeletonEditingToolClipboardLocals;
	
		FString Content;
		FPlatformApplicationMisc::ClipboardPaste(Content);

		const bool bWorld = !RelativeArray[InComponent];
		FTransform Xfo = Tool->GetTransform(InBoneName, bWorld);
		
		if (Content.IsEmpty())
		{
			return MakeTuple(Xfo, Xfo);
		}

		switch(InComponent)
		{
		case ESlateTransformComponent::Location:
			{
				FVector Data = Xfo.GetLocation();
				if (GetDataFromContent(Content, Data))
				{
					Xfo.SetLocation(Data);
				}
				break;
			}
		case ESlateTransformComponent::Rotation:
			{
				FRotator Data = Xfo.Rotator();
				if (GetDataFromContent(Content, Data))
				{
					Xfo.SetRotation(FQuat(Data));
				}
				break;
			}
		case ESlateTransformComponent::Scale:
			{
				FVector Data = Xfo.GetScale3D();
				if (GetDataFromContent(Content, Data))
				{
					Xfo.SetScale3D(Data);
				}
				break;
			}
		case ESlateTransformComponent::Max:
		default:
			{
				FTransform Data = Xfo;
				if (GetDataFromContent(Content, Data))
				{
					Xfo = Data;
				}
				break;
			}
		}

		return MakeTuple(Tool->GetTransform(InBoneName, bWorld), Xfo);
	};

	InOutArgs.OnCopyToClipboard_Lambda( [this, OnCopyToClipboard](ESlateTransformComponent::Type InComponent)
	{
		const TArray<FName>& Bones = Tool->GetSelection();
		if (!Bones.IsEmpty())
		{
			OnCopyToClipboard(Bones[0], InComponent);
		}
	})
	.OnPasteFromClipboard_Lambda([this, PreparePasteFromClipboard](ESlateTransformComponent::Type InComponent)
	{
		const TArray<FName>& Bones = Tool->GetSelection();
		TArray<FName> BonesToMove; BonesToMove.Reserve(Bones.Num());
		TArray<FTransform> UpdatedTransforms; UpdatedTransforms.Reserve(Bones.Num());
		
		FTransform CurrentTransform, UpdatedTransform;
		for (const FName& BoneName: Bones)
		{
			Tie(CurrentTransform, UpdatedTransform) = PreparePasteFromClipboard(BoneName, InComponent);
			if (!UpdatedTransform.Equals(CurrentTransform))
			{
				BonesToMove.Add(BoneName);
				UpdatedTransforms.Add(UpdatedTransform);
			}
		}

		if (BonesToMove.IsEmpty())
		{
			return;
		}
		
		if (!ActiveChange.IsValid())
		{
			ActiveChange = MakeUnique<SkeletonEditingTool::FRefSkeletonChange>(Tool.Get());
		}
		
		const bool bWorld = !RelativeArray[InComponent];
		Tool->SetTransforms(BonesToMove, UpdatedTransforms, bWorld);

		// send transaction
		if (UInteractiveToolManager* ToolManager = Tool->GetToolManager())
		{
			ActiveChange->StoreSkeleton(Tool.Get());
			
			static const FText TransactionDesc = LOCTEXT("PasteTransform", "Paste Transform");
			ToolManager->BeginUndoTransaction(TransactionDesc);
			ToolManager->EmitObjectChange(Tool.Get(), MoveTemp(ActiveChange), TransactionDesc);
			ToolManager->EndUndoTransaction();
		}
		ActiveChange.Reset();
	});
}

#undef LOCTEXT_NAMESPACE
