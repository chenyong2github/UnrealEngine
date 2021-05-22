// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayInsightsDebugViewCreator.h"
#include "IGameplayProvider.h"

namespace TraceServices
{
    class IAnalysisSession;
}

void FGameplayInsightsDebugViewCreator::RegisterDebugViewCreator(FName TypeName, FCreateDebugView Creator)
{
    ViewCreators.Add(TypeName, Creator);
}

void FGameplayInsightsDebugViewCreator::CreateDebugViews(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& Session, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews) 
{
    if (const IGameplayProvider* GameplayProvider = Session.ReadProvider<IGameplayProvider>("GameplayProvider"))
    {
		TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

        const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(ObjectId);
        uint64 ClassId = ObjectInfo.ClassId;

        while (ClassId != 0)
        {
            const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);

            TArray<FCreateDebugView> List;
            ViewCreators.MultiFind(ClassInfo.Name, List);

            for (FCreateDebugView& Creator : List)
            {
                if (Creator.IsBound())
                {
                    OutDebugViews.Add(Creator.Execute(ObjectId, CurrentTime, Session));
                }
            }

            ClassId = ClassInfo.SuperId;
        }
    }
}