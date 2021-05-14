// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customization/BlendSpaceDetails.h"

#include "IDetailsView.h"
#include "IDetailGroup.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"

#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"

#include "BlendSampleDetails.h"

#include "Widgets/Input/SNumericEntryBox.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "BlendSpaceGraph.h"
#include "AnimGraphNode_BlendSpaceGraph.h"

#define LOCTEXT_NAMESPACE "BlendSpaceDetails"

FBlendSpaceDetails::FBlendSpaceDetails()
{
	Builder = nullptr;
	BlendSpaceBase = nullptr;
	BlendSpaceNode = nullptr;
}

FBlendSpaceDetails::~FBlendSpaceDetails()
{
}

void FBlendSpaceDetails::CustomizeDetails(class IDetailLayoutBuilder& DetailBuilder)
{
	TArray< TWeakObjectPtr<UObject> > Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	Builder = &DetailBuilder;
	TWeakObjectPtr<UObject>* WeakPtr = Objects.FindByPredicate([](const TWeakObjectPtr<UObject>& ObjectPtr) { return ObjectPtr->IsA<UBlendSpace>(); });
	if (WeakPtr)
	{
		BlendSpaceBase = Cast<UBlendSpace>(WeakPtr->Get());

		if(!BlendSpaceBase->IsAsset())
		{
			// Hide various properties when we are 'internal'
			DetailBuilder.HideCategory("MetaData");
			DetailBuilder.HideCategory("AnimationNotifies");
			DetailBuilder.HideCategory("Thumbnail");
			DetailBuilder.HideCategory("Animation");
			DetailBuilder.HideCategory("AdditiveSettings");
		}

		if(UBlendSpaceGraph* BlendSpaceGraph = Cast<UBlendSpaceGraph>(BlendSpaceBase->GetOuter()))
		{
			check(BlendSpaceBase == BlendSpaceGraph->BlendSpace);
			BlendSpaceNode = Cast<UAnimGraphNode_BlendSpaceGraphBase>(BlendSpaceGraph->GetOuter());
		}
		const bool b1DBlendSpace = BlendSpaceBase->IsA<UBlendSpace1D>();

		if (b1DBlendSpace)
		{
			DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBlendSpace, AxisToScaleAnimation), UBlendSpace::StaticClass()));
			DetailBuilder.HideProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBlendSpace, PreferredTriangulationDirection), UBlendSpace::StaticClass()));
		}

		IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(FName("Axis Settings"));
		IDetailGroup* Groups[2] =
		{
			&CategoryBuilder.AddGroup(FName("Horizontal Axis"), LOCTEXT("HorizontalAxisName", "Horizontal Axis")),
			b1DBlendSpace ? nullptr : &CategoryBuilder.AddGroup(FName("Vertical Axis"), LOCTEXT("VerticalAxisName", "Vertical Axis"))
		};

		// Hide the default blend and interpolation parameters
		TSharedPtr<IPropertyHandle> BlendParameters = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBlendSpace, BlendParameters), UBlendSpace::StaticClass());
		TSharedPtr<IPropertyHandle> InterpolationParameters = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBlendSpace, InterpolationParam), UBlendSpace::StaticClass());
		DetailBuilder.HideProperty(BlendParameters);
		DetailBuilder.HideProperty(InterpolationParameters);

		// Add the properties to the corresponding groups created above (third axis will always be hidden since it isn't used)
		int32 HideIndex = b1DBlendSpace ? 1 : 2;
		for (int32 AxisIndex = 0; AxisIndex < 3; ++AxisIndex)
		{
			TSharedPtr<IPropertyHandle> BlendParameter = BlendParameters->GetChildHandle(AxisIndex);
			TSharedPtr<IPropertyHandle> InterpolationParameter = InterpolationParameters->GetChildHandle(AxisIndex);

			if (AxisIndex < HideIndex)
			{
				Groups[AxisIndex]->AddPropertyRow(BlendParameter.ToSharedRef());
				// Don't add InterpolationParameter in the same way as BlendParameter, because it would add the
				// elements as customizations that we can't subsequently customize. We will add them individually
				// below.

				TSharedPtr<IPropertyHandle> InterpolationTime = InterpolationParameter->GetChildHandle(
                    GET_MEMBER_NAME_CHECKED(FInterpolationParameter, InterpolationTime));
				TSharedPtr<IPropertyHandle> DampingRatio = InterpolationParameter->GetChildHandle(
                    GET_MEMBER_NAME_CHECKED(FInterpolationParameter, DampingRatio));
				TSharedPtr<IPropertyHandle> MaxSpeed = InterpolationParameter->GetChildHandle(
                    GET_MEMBER_NAME_CHECKED(FInterpolationParameter, MaxSpeed));
				TSharedPtr<IPropertyHandle> InterpolationType = InterpolationParameter->GetChildHandle(
                    GET_MEMBER_NAME_CHECKED(FInterpolationParameter, InterpolationType));

				// Custom edit condition for MaxSpeed
				TAttribute<bool> MaxSpeedEditCondition = TAttribute<bool>::Create(
                    [this, InterpolationTime, InterpolationType]()
                    {
                    uint8 IntType;
                    InterpolationType->GetValue(IntType);
                    EFilterInterpolationType Type = (EFilterInterpolationType) IntType;
                    float Time;
                    InterpolationTime->GetValue(Time);
                    if (Time > 0.0f && (Type == EFilterInterpolationType::BSIT_SpringDamper ||
                        Type == EFilterInterpolationType::BSIT_ExponentialDecay))
                    {
                        return true;
                    }
                    return false;
                });

				Groups[AxisIndex]->AddPropertyRow(InterpolationTime.ToSharedRef());
				Groups[AxisIndex]->AddPropertyRow(InterpolationType.ToSharedRef());
				Groups[AxisIndex]->AddPropertyRow(DampingRatio.ToSharedRef());
				IDetailPropertyRow& MaxSpeedProperty = Groups[AxisIndex]->AddPropertyRow(MaxSpeed.ToSharedRef());
				MaxSpeedProperty.EditCondition(MaxSpeedEditCondition, nullptr);
			}
			else
			{
				DetailBuilder.HideProperty(BlendParameter);
				DetailBuilder.HideProperty(InterpolationParameter);
			}
		}

		IDetailCategoryBuilder& SampleCategoryBuilder = DetailBuilder.EditCategory(FName("BlendSamples"));
		TArray<TSharedRef<IPropertyHandle>> DefaultProperties;
		SampleCategoryBuilder.GetDefaultProperties(DefaultProperties);
		for (TSharedRef<IPropertyHandle> DefaultProperty : DefaultProperties)
		{
			DefaultProperty->MarkHiddenByCustomization();
		}

		FSimpleDelegate RefreshDelegate = FSimpleDelegate::CreateLambda([this]() { Builder->ForceRefreshDetails(); });

		// Retrieve blend samples array
		TSharedPtr<IPropertyHandleArray> BlendSamplesArrayProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UBlendSpace, SampleData), UBlendSpace::StaticClass())->AsArray();
		BlendSamplesArrayProperty->SetOnNumElementsChanged(RefreshDelegate);
		
		uint32 NumBlendSampleEntries = 0;
		BlendSamplesArrayProperty->GetNumElements(NumBlendSampleEntries);
		for (uint32 SampleIndex = 0; SampleIndex < NumBlendSampleEntries; ++SampleIndex)
		{
			TSharedPtr<IPropertyHandle> BlendSampleProperty = BlendSamplesArrayProperty->GetElement(SampleIndex);
			BlendSampleProperty->SetOnChildPropertyValueChanged(RefreshDelegate);
			TSharedPtr<IPropertyHandle> AnimationProperty = BlendSampleProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBlendSample, Animation));
			TSharedPtr<IPropertyHandle> SampleValueProperty = BlendSampleProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBlendSample, SampleValue));
			TSharedPtr<IPropertyHandle> RateScaleProperty = BlendSampleProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FBlendSample, RateScale));

			IDetailGroup& Group = SampleCategoryBuilder.AddGroup(FName("GroupName"), FText::GetEmpty());
			Group.HeaderRow()
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(FMargin(0,2,2,2))
				.FillWidth(1.0f)
				.HAlign(HAlign_Right)
				[
					SNew(STextBlock)
					.Font(DetailBuilder.GetDetailFont())
					.Text_Lambda([this, AnimationProperty, SampleIndex]() -> FText
					{
						FAssetData AssetData;
						AnimationProperty->GetValue(AssetData);
						if(AssetData.IsValid())
						{
							return FText::Format(LOCTEXT("BlendSpaceAnimationNameLabel", "{0} ({1})"), FText::FromString(AssetData.GetAsset()->GetName()), FText::FromString(FString::FromInt(SampleIndex)));
						}
						else if(BlendSpaceNode.Get() && BlendSpaceNode->GetGraphs().IsValidIndex(SampleIndex))
						{
							return FText::Format(LOCTEXT("BlendSpaceAnimationNameLabel", "{0} ({1})"), FText::FromName(BlendSpaceNode->GetGraphs()[SampleIndex]->GetFName()), FText::FromString(FString::FromInt(SampleIndex)));
						}
						return LOCTEXT("NoAnimation", "No Animation");
					})
				]
			];

			FBlendSampleDetails::GenerateBlendSampleWidget([&Group]() -> FDetailWidgetRow& { return Group.AddWidgetRow(); }, FOnSampleMoved::CreateLambda([this](const uint32 Index, const FVector& SampleValue, bool bIsInteractive) 
			{
				if (BlendSpaceBase->IsValidBlendSampleIndex(Index) && BlendSpaceBase->GetBlendSample(Index).SampleValue != SampleValue && !BlendSpaceBase->IsTooCloseToExistingSamplePoint(SampleValue, Index))
				{
					BlendSpaceBase->Modify();

					bool bMoveSuccesful = BlendSpaceBase->EditSampleValue(Index, SampleValue);
					if (bMoveSuccesful)
					{
						BlendSpaceBase->ValidateSampleData();
						FPropertyChangedEvent ChangedEvent(nullptr, bIsInteractive ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet);
						BlendSpaceBase->PostEditChangeProperty(ChangedEvent);
					}
				}
			}), BlendSpaceBase, SampleIndex, false);
			
			if(BlendSpaceBase->IsAsset())
			{
				FDetailWidgetRow& AnimationRow = Group.AddWidgetRow();
				FBlendSampleDetails::GenerateAnimationWidget(AnimationRow, BlendSpaceBase, AnimationProperty);
				Group.AddPropertyRow(RateScaleProperty.ToSharedRef());
			}
			else if(BlendSpaceNode.Get())
			{
				FDetailWidgetRow& GraphRow = Group.AddWidgetRow();
				FBlendSampleDetails::GenerateSampleGraphWidget(GraphRow, BlendSpaceNode.Get(), SampleIndex);
			}
		}
	}
	
}

#undef LOCTEXT_NAMESPACE
