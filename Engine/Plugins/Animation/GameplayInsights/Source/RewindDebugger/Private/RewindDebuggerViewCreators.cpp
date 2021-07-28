// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerViewCreators.h"
#include "Features/IModularFeatures.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "IGameplayProvider.h"

void FRewindDebuggerViewCreators::EnumerateCreators(TFunctionRef<void(const IRewindDebuggerViewCreator*)> Callback)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	static const FName CreatorFeatureName = IRewindDebuggerViewCreator::ModularFeatureName;

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(CreatorFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerViewCreator* ViewCreator = static_cast<IRewindDebuggerViewCreator*>(ModularFeatures.GetModularFeatureImplementation(CreatorFeatureName, ExtensionIndex));
        Callback(ViewCreator);
    }
}

void FRewindDebuggerViewCreators::CreateDebugViews(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& Session, TArray<TSharedPtr<IRewindDebuggerView>>& OutDebugViews)
{
	TraceServices::FAnalysisSessionReadScope SessionReadScope(Session);

	const IGameplayProvider* GameplayProvider = Session.ReadProvider<IGameplayProvider>("GameplayProvider");
	const FObjectInfo& ObjectInfo = GameplayProvider->GetObjectInfo(ObjectId);
	uint64 ClassId = ObjectInfo.ClassId;
	TArray<FName> TypeNameHierarchy;
	while (ClassId != 0)
	{
		const FClassInfo& ClassInfo = GameplayProvider->GetClassInfo(ClassId);
		TypeNameHierarchy.Add(ClassInfo.Name);
		ClassId = ClassInfo.SuperId;
	}

	EnumerateCreators([&OutDebugViews, &TypeNameHierarchy, ObjectId, CurrentTime, &Session](const IRewindDebuggerViewCreator* ViewCreator)
	{
		if (TypeNameHierarchy.Contains(ViewCreator->GetTargetTypeName()))
		{
			OutDebugViews.Add(ViewCreator->CreateDebugView(ObjectId, CurrentTime, Session));
		}
	});
}

const IRewindDebuggerViewCreator* FRewindDebuggerViewCreators::GetCreator(FName CreatorName)
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	static const FName CreatorFeatureName = IRewindDebuggerViewCreator::ModularFeatureName;

	const int32 NumExtensions = ModularFeatures.GetModularFeatureImplementationCount(CreatorFeatureName);
	for (int32 ExtensionIndex = 0; ExtensionIndex < NumExtensions; ++ExtensionIndex)
	{
		IRewindDebuggerViewCreator* ViewCreator = static_cast<IRewindDebuggerViewCreator*>(ModularFeatures.GetModularFeatureImplementation(CreatorFeatureName, ExtensionIndex));
		if (ViewCreator->GetName() == CreatorName)
		{
			return ViewCreator;
		}
    }

    return nullptr;
}
