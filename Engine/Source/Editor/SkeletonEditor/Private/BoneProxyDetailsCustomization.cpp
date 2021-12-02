// Copyright Epic Games, Inc. All Rights Reserved.

#include "BoneProxyDetailsCustomization.h"
#include "BoneProxy.h"
#include "AnimPreviewInstance.h"
#include "PropertyHandle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBox.h"
#include "Algo/Transform.h"
#include "SAdvancedTransformInputBox.h"

#define LOCTEXT_NAMESPACE "FBoneProxyDetailsCustomization"

namespace BoneProxyCustomizationConstants
{
	static const float ItemWidth = 125.0f;
}

static FText GetTransformFieldText(bool* bValuePtr, FText Label)
{
	return *bValuePtr ? FText::Format(LOCTEXT("Local", "Local {0}"), Label) : FText::Format(LOCTEXT("World", "World {0}"), Label);
}

static void OnSetRelativeTransform(bool* bValuePtr)
{
	*bValuePtr = true;
}

static void OnSetWorldTransform(bool* bValuePtr)
{
	*bValuePtr = false;
}

static bool IsRelativeTransformChecked(bool* bValuePtr)
{
	return *bValuePtr;
}

static bool IsWorldTransformChecked(bool* bValuePtr)
{
	return !*bValuePtr;
}

static TSharedRef<SWidget> BuildTransformFieldLabel(bool* bValuePtr, const FText& Label, bool bMultiSelected)
{
	if (bMultiSelected)
	{
		return SNew(STextBlock)
			.Text(Label)
			.Font(IDetailLayoutBuilder::GetDetailFont());
	}
	else
	{
		FMenuBuilder MenuBuilder(true, nullptr, nullptr);

		FUIAction SetRelativeLocationAction
		(
			FExecuteAction::CreateStatic(&OnSetRelativeTransform, bValuePtr),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&IsRelativeTransformChecked, bValuePtr)
		);

		FUIAction SetWorldLocationAction
		(
			FExecuteAction::CreateStatic(&OnSetWorldTransform, bValuePtr),
			FCanExecuteAction(),
			FIsActionChecked::CreateStatic(&IsWorldTransformChecked, bValuePtr)
		);

		MenuBuilder.BeginSection( TEXT("TransformType"), FText::Format( LOCTEXT("TransformType", "{0} Type"), Label ) );

		MenuBuilder.AddMenuEntry
		(
			FText::Format( LOCTEXT( "LocalLabel", "Local"), Label ),
			FText::Format( LOCTEXT( "LocalLabel_ToolTip", "{0} is relative to its parent"), Label ),
			FSlateIcon(),
			SetRelativeLocationAction,
			NAME_None, 
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.AddMenuEntry
		(
			FText::Format( LOCTEXT( "WorldLabel", "World"), Label ),
			FText::Format( LOCTEXT( "WorldLabel_ToolTip", "{0} is relative to the world"), Label ),
			FSlateIcon(),
			SetWorldLocationAction,
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);

		MenuBuilder.EndSection();

		return 
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			[
				SNew(SComboButton)
				.ContentPadding( 0 )
				.ButtonStyle( FEditorStyle::Get(), "NoBorder" )
				.ForegroundColor( FSlateColor::UseForeground() )
				.MenuContent()
				[
					MenuBuilder.MakeWidget()
				]
				.ButtonContent()
				[
					SNew( SBox )
					.Padding( FMargin( 0.0f, 0.0f, 2.0f, 0.0f ) )
					[
						SNew(STextBlock)
						.Text_Static(&GetTransformFieldText, bValuePtr, Label)
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				]
			];
	}
}

void FBoneProxyDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	BoneProxies.Empty();
	Algo::TransformIf(Objects, BoneProxies, [](TWeakObjectPtr<UObject> InItem) { return InItem.IsValid() && InItem.Get()->IsA<UBoneProxy>(); }, [](TWeakObjectPtr<UObject> InItem) { return CastChecked<UBoneProxy>(InItem.Get()); });
	TArrayView<UBoneProxy*> BoneProxiesView(BoneProxies);

	UBoneProxy* FirstBoneProxy = CastChecked<UBoneProxy>(Objects[0].Get());

	bool bIsEditingEnabled = true;
	if (UDebugSkelMeshComponent* Component = FirstBoneProxy->SkelMeshComponent.Get())
	{
		bIsEditingEnabled = (Component->AnimScriptInstance == Component->PreviewInstance);
	}
	
	DetailBuilder.HideCategory(TEXT("Transform"));
	DetailBuilder.HideCategory(TEXT("Reference Transform"));
	DetailBuilder.HideCategory(TEXT("Mesh Relative Transform"));
	DetailBuilder.EditCategory(TEXT("Bone")).SetSortOrder(1);

	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(TEXT("Transforms"));
	CategoryBuilder.SetSortOrder(2);

	const TArray<FText> ButtonLabels =
	{
		LOCTEXT("BoneTransform", "Bone"),
		LOCTEXT("ReferenceTransform", "Reference"),
		LOCTEXT("MeshTransform", "Mesh Relative")
	};
	const TArray<FText> ButtonTooltips =
	{
		LOCTEXT("BoneTransformTooltip", "The transform of the bone"),
		LOCTEXT("ReferenceTransformTooltip", "The reference transform of a bone (original)"),
		LOCTEXT("MeshTransformTooltip", "The relative transform of the mesh")
	};

	TSharedPtr<SSegmentedControl<UBoneProxy::ETransformType>> TransformChoiceWidget;

	// use a static shared ref so that all views retain these settings
	static TSharedRef<TArray<UBoneProxy::ETransformType>> VisibleTransforms =
		MakeShareable(new TArray<UBoneProxy::ETransformType>({
			UBoneProxy::TransformType_Bone,
			UBoneProxy::TransformType_Reference,
			UBoneProxy::TransformType_Mesh}));

	CategoryBuilder.AddCustomRow(FText::FromString(TEXT("TransformType")))
	.ValueContent()
	.MinDesiredWidth(375.f)
	.MaxDesiredWidth(375.f)
	.HAlign(HAlign_Left)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SAssignNew(TransformChoiceWidget, SSegmentedControl<UBoneProxy::ETransformType>)
			.SupportsMultiSelection(true)
			.Values_Lambda([]()
			{
				return VisibleTransforms.Get();
			})
			.OnValuesChanged_Lambda([](TArray<UBoneProxy::ETransformType> Values)
			{
				VisibleTransforms.Get() = Values;
			})
			+ SSegmentedControl<UBoneProxy::ETransformType>::Slot(UBoneProxy::TransformType_Bone)
			.Text(ButtonLabels[0])
			.ToolTip(ButtonTooltips[0])
			+ SSegmentedControl<UBoneProxy::ETransformType>::Slot(UBoneProxy::TransformType_Reference)
			.Text(ButtonLabels[1])
			.ToolTip(ButtonTooltips[1])
			+ SSegmentedControl<UBoneProxy::ETransformType>::Slot(UBoneProxy::TransformType_Mesh)
			.Text(ButtonLabels[2])
			.ToolTip(ButtonTooltips[2])
		]
	];

	SAdvancedTransformInputBox<FEulerTransform>::FArguments TransformWidgetArgs = SAdvancedTransformInputBox<FEulerTransform>::FArguments()
	.DisplayRelativeWorld(true)
	.AllowEditRotationRepresentation(false)
	.DisplayScaleLock(true)
	.Font(IDetailLayoutBuilder::GetDetailFont())
	.UseQuaternionForRotation(false)
	.OnGetIsComponentRelative_Lambda(
		[BoneProxiesView](ESlateTransformComponent::Type InComponent)
		{
			switch(InComponent)
			{
				case ESlateTransformComponent::Location:
					return BoneProxiesView[0]->bLocalLocation;
				case ESlateTransformComponent::Rotation:
					return BoneProxiesView[0]->bLocalRotation;
				case ESlateTransformComponent::Scale:
					return BoneProxiesView[0]->bLocalScale;
			}
			return true;
		})
	.OnIsComponentRelativeChanged_Lambda(
		[BoneProxiesView](ESlateTransformComponent::Type InComponent, bool bIsRelative)
		{
			for(UBoneProxy* BoneProxy : BoneProxiesView)
			{
				switch(InComponent)
				{
					case ESlateTransformComponent::Location:
					{
						BoneProxy->bLocalLocation = bIsRelative;
						break;
					}
					case ESlateTransformComponent::Rotation:
					{
						BoneProxy->bLocalRotation = bIsRelative;
						break;
					}
					case ESlateTransformComponent::Scale:
					{
						BoneProxy->bLocalScale = bIsRelative;
						break;
					}
				}
			}
		});

	TArray<TSharedRef<IPropertyHandle>> Properties;
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, Location)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, Rotation)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, Scale)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, ReferenceLocation)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, ReferenceRotation)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, ReferenceScale)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, MeshLocation)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, MeshRotation)));
	Properties.Add(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBoneProxy, MeshScale)));

	int32 PropertyIndex = 0;
	for(int32 TransformIndex=0;TransformIndex<3;TransformIndex++)
	{
		// only the first transform can be edited
		const UBoneProxy::ETransformType TransformType = (UBoneProxy::ETransformType)TransformIndex; 
		bIsEditingEnabled = TransformType == UBoneProxy::TransformType_Bone ? bIsEditingEnabled : false;

		TransformWidgetArgs
		.IsEnabled(bIsEditingEnabled)
		.DisplayRelativeWorld(bIsEditingEnabled)
		.DisplayScaleLock(bIsEditingEnabled)
		.OnGetNumericValue_Static(&UBoneProxy::GetMultiNumericValue, TransformType, BoneProxiesView);

		if(bIsEditingEnabled)
		{
			TransformWidgetArgs.OnNumericValueChanged_Static(&UBoneProxy::OnMultiNumericValueCommitted, ETextCommit::Default, TransformType, BoneProxiesView, false);
			TransformWidgetArgs.OnNumericValueCommitted_Static (&UBoneProxy::OnMultiNumericValueCommitted, TransformType, BoneProxiesView, true);
		}
		else
		{
			TransformWidgetArgs._OnNumericValueChanged.Unbind();
			TransformWidgetArgs._OnNumericValueCommitted.Unbind();
		}
		
		IDetailGroup& Group = CategoryBuilder.AddGroup(*ButtonLabels[TransformIndex].ToString(), ButtonLabels[TransformIndex], false, true);
		Group.HeaderRow()
		.Visibility( TAttribute<EVisibility>::CreateLambda([TransformChoiceWidget, TransformType]() -> EVisibility
		{
			return TransformChoiceWidget->HasValue(TransformType) ? EVisibility::Visible : EVisibility::Collapsed;
		}))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(ButtonLabels[TransformIndex])
			.ToolTipText(ButtonTooltips[TransformIndex])
		];

		IDetailPropertyRow& LocationPropertyRow = Group.AddPropertyRow(Properties[PropertyIndex++]);
		if(bIsEditingEnabled)
		{
			LocationPropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FIsResetToDefaultVisible::CreateSP(this, &FBoneProxyDetailsCustomization::IsResetLocationVisible, BoneProxiesView), FResetToDefaultHandler::CreateSP(this, &FBoneProxyDetailsCustomization::HandleResetLocation, BoneProxiesView)));
		}

		LocationPropertyRow.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Location)
		]
		.ValueContent()
		.MinDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Location)
		];

		IDetailPropertyRow& RotationPropertyRow = Group.AddPropertyRow(Properties[PropertyIndex++]);
		if(bIsEditingEnabled)
		{
			RotationPropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FIsResetToDefaultVisible::CreateSP(this, &FBoneProxyDetailsCustomization::IsResetRotationVisible, BoneProxiesView), FResetToDefaultHandler::CreateSP(this, &FBoneProxyDetailsCustomization::HandleResetRotation, BoneProxiesView)));
		}

		RotationPropertyRow.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		]
		.ValueContent()
		.MinDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Rotation)
		];

		IDetailPropertyRow& ScalePropertyRow = Group.AddPropertyRow(Properties[PropertyIndex++]);
		if(bIsEditingEnabled)
		{
			ScalePropertyRow.OverrideResetToDefault(FResetToDefaultOverride::Create(FIsResetToDefaultVisible::CreateSP(this, &FBoneProxyDetailsCustomization::IsResetScaleVisible, BoneProxiesView), FResetToDefaultHandler::CreateSP(this, &FBoneProxyDetailsCustomization::HandleResetScale, BoneProxiesView)));
		}

		ScalePropertyRow.CustomWidget()
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructLabel(TransformWidgetArgs, ESlateTransformComponent::Scale)
		]
		.ValueContent()
		.MinDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		.MaxDesiredWidth(BoneProxyCustomizationConstants::ItemWidth * 3.0f)
		[
			SAdvancedTransformInputBox<FEulerTransform>::ConstructWidget(TransformWidgetArgs, ESlateTransformComponent::Scale)
		];
	}
}

bool FBoneProxyDetailsCustomization::IsResetLocationVisible(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			if (FAnimNode_ModifyBone* ModifyBone = Component->PreviewInstance->FindModifiedBone(BoneProxy->BoneName))
			{
				if (ModifyBone->Translation != FVector::ZeroVector)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool FBoneProxyDetailsCustomization::IsResetRotationVisible(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			if (FAnimNode_ModifyBone* ModifyBone = Component->PreviewInstance->FindModifiedBone(BoneProxy->BoneName))
			{
				if (ModifyBone->Rotation != FRotator::ZeroRotator)
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool FBoneProxyDetailsCustomization::IsResetScaleVisible(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			if (FAnimNode_ModifyBone* ModifyBone = Component->PreviewInstance->FindModifiedBone(BoneProxy->BoneName))
			{
				if (ModifyBone->Scale != FVector(1.0f))
				{
					return true;
				}
			}
		}
	}

	return false;
}

void FBoneProxyDetailsCustomization::HandleResetLocation(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	FScopedTransaction Transaction(LOCTEXT("ResetLocation", "Reset Location"));

	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			BoneProxy->Modify();
			Component->PreviewInstance->Modify();

			FAnimNode_ModifyBone& ModifyBone = Component->PreviewInstance->ModifyBone(BoneProxy->BoneName);
			ModifyBone.Translation = FVector::ZeroVector;

			RemoveUnnecessaryModifications(Component, ModifyBone);
		}
	}
}

void FBoneProxyDetailsCustomization::HandleResetRotation(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	FScopedTransaction Transaction(LOCTEXT("ResetRotation", "Reset Rotation"));

	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			BoneProxy->Modify();
			Component->PreviewInstance->Modify();

			FAnimNode_ModifyBone& ModifyBone = Component->PreviewInstance->ModifyBone(BoneProxy->BoneName);
			ModifyBone.Rotation = FRotator::ZeroRotator;

			RemoveUnnecessaryModifications(Component, ModifyBone);
		}
	}
}

void FBoneProxyDetailsCustomization::HandleResetScale(TSharedPtr<IPropertyHandle> InPropertyHandle, TArrayView<UBoneProxy*> InBoneProxies)
{
	FScopedTransaction Transaction(LOCTEXT("ResetScale", "Reset Scale"));

	for (UBoneProxy* BoneProxy : InBoneProxies)
	{
		if (UDebugSkelMeshComponent* Component = BoneProxy->SkelMeshComponent.Get())
		{
			BoneProxy->Modify();
			Component->PreviewInstance->Modify();

			FAnimNode_ModifyBone& ModifyBone = Component->PreviewInstance->ModifyBone(BoneProxy->BoneName);
			ModifyBone.Scale = FVector(1.0f);

			RemoveUnnecessaryModifications(Component, ModifyBone);
		}
	}
}

void FBoneProxyDetailsCustomization::RemoveUnnecessaryModifications(UDebugSkelMeshComponent* Component, FAnimNode_ModifyBone& ModifyBone)
{
	if (ModifyBone.Translation == FVector::ZeroVector && ModifyBone.Rotation == FRotator::ZeroRotator && ModifyBone.Scale == FVector(1.0f))
	{
		Component->PreviewInstance->RemoveBoneModification(ModifyBone.BoneToModify.BoneName);
	}
}

#undef LOCTEXT_NAMESPACE
