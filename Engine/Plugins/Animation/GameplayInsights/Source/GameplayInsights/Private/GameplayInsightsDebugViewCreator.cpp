// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInsightsDebugViewCreator.h"
#include "IGameplayProvider.h"

namespace TraceServices
{
    class IAnalysisSession;
}

void FGameplayInsightsDebugViewCreator::RegisterDebugViewCreator(FName TypeName, TSharedPtr<ICreateGameplayInsightsDebugView> Creator)
{
    FViewCreatorPair Pair;
    Pair.Creator = Creator;
    Pair.TypeName = TypeName;

    ViewCreators.Add(Pair);
}

void FGameplayInsightsDebugViewCreator::EnumerateCreators(TFunctionRef<void(const TSharedPtr<ICreateGameplayInsightsDebugView>&)> Callback) const
{
    for(const FViewCreatorPair& CreatorPair : ViewCreators)
    {
        if (CreatorPair.Creator.IsValid())
        {
            Callback(CreatorPair.Creator);
        }
    }
}

TSharedPtr<ICreateGameplayInsightsDebugView> FGameplayInsightsDebugViewCreator::GetCreator(FName CreatorName) const
{
    for(const FViewCreatorPair& CreatorPair : ViewCreators)
    {
        if (CreatorPair.Creator.IsValid() && CreatorPair.Creator->GetName() == CreatorName)
        {
            return CreatorPair.Creator;
        }
    }

    return nullptr;
}

void FGameplayInsightsDebugViewCreator::CreateDebugViews(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& Session, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews) const
{
    if (const IGameplayProvider* GameplayProvider = Session.ReadProvider<IGameplayProvider>("GameplayProvider"))
    {
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

        const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(ObjectId);
        uint64 ClassId = ObjectInfo.ClassId;

        while (ClassId != 0)
        {
            const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);

            for(const FViewCreatorPair& CreatorPair : ViewCreators)
            {
                if (CreatorPair.Creator.IsValid() && CreatorPair.TypeName == ClassInfo.Name)
                {
                    OutDebugViews.Add(CreatorPair.Creator->CreateDebugView(ObjectId, CurrentTime, Session));
                }
            }

            ClassId = ClassInfo.SuperId;
        }
    }
}