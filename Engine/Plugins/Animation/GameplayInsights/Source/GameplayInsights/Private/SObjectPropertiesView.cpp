// Copyright Epic Games, Inc. All Rights Reserved.

#include "SObjectPropertiesView.h"
#include "AnimationProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplayProvider.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"
#include "VariantTreeNode.h"

#define LOCTEXT_NAMESPACE "SObjectPropertiesView"

void SObjectPropertiesView::GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const
{
	if (const FGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName))
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		const FClassInfo& ClassInfo = GameplayProvider->GetClassInfoFromObject(ObjectId);

		if(ClassInfo.Properties.Num() > 0)
		{
			TSharedRef<FVariantTreeNode> Header = OutVariants.Add_GetRef(FVariantTreeNode::MakeHeader(LOCTEXT("Properties", "Properties"), INDEX_NONE));

			// Build the class tree
			TArray<TSharedPtr<FVariantTreeNode>> PropertyVariants;
			PropertyVariants.SetNum(ClassInfo.Properties.Num());
			for(int32 PropertyIndex = 0; PropertyIndex < ClassInfo.Properties.Num(); ++PropertyIndex)
			{
				const FClassPropertyInfo& PropertyInfo = ClassInfo.Properties[PropertyIndex];

				// Add a string node with a default value
				const TCHAR* Key = GameplayProvider->GetPropertyName(PropertyInfo.KeyStringId);
				PropertyVariants[PropertyIndex] = FVariantTreeNode::MakeString(FText::FromString(Key), TEXT("Unknown"), PropertyIndex);

				// note assumes that order is parent->child in the properties array
				if(PropertyInfo.ParentId != INDEX_NONE)
				{
					PropertyVariants[PropertyInfo.ParentId]->AddChild(PropertyVariants[PropertyIndex].ToSharedRef());
				}
				else
				{
					Header->AddChild(PropertyVariants[PropertyIndex].ToSharedRef());
				}
			}

			// object events
			GameplayProvider->ReadObjectPropertiesTimeline(ObjectId, [this, &InFrame, &GameplayProvider, &PropertyVariants](const FGameplayProvider::ObjectPropertiesTimeline& InTimeline)
			{
				InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [this, &GameplayProvider, &PropertyVariants](double InStartTime, double InEndTime, uint32 InDepth, const FObjectPropertiesMessage& InMessage)
				{
					GameplayProvider->EnumerateObjectPropertyValues(ObjectId, InMessage, [&PropertyVariants](const FObjectPropertyValue& InValue)
					{
						PropertyVariants[InValue.PropertyId]->GetValue().String.Value = InValue.Value;
					});
					return TraceServices::EEventEnumerate::Stop;
				});
			});
		}
	}
}


static const FName ObjectPropertiesName("ObjectProperties");

FName SObjectPropertiesView::GetName() const
{
	return ObjectPropertiesName;
}

FName FObjectPropertiesViewCreator::GetName() const
{
	return ObjectPropertiesName;
}

FText FObjectPropertiesViewCreator::GetTitle() const
{
	return LOCTEXT("Object Properties", "Properties");
}

FSlateIcon FObjectPropertiesViewCreator::GetIcon() const
{
	return FSlateIconFinder::FindIconForClass(UObject::StaticClass());
}

TSharedPtr<IGameplayInsightsDebugView> FObjectPropertiesViewCreator::CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& AnalysisSession) const
{
	return SNew(SObjectPropertiesView, ObjectId, CurrentTime, AnalysisSession);
}


#undef LOCTEXT_NAMESPACE
