// Copyright Epic Games, Inc. All Rights Reserved.

#include "DerivedDataBuildFunctionRegistry.h"

#include "Containers/Map.h"
#include "Containers/UnrealString.h"
#include "DerivedDataBuildFunction.h"
#include "DerivedDataBuildFunctionFactory.h"
#include "DerivedDataBuildPrivate.h"
#include "Features/IModularFeatures.h"
#include "HAL/CriticalSection.h"
#include "Misc/Guid.h"
#include "Misc/ScopeRWLock.h"

namespace UE::DerivedData::Private
{

class FBuildFunctionRegistry : public IBuildFunctionRegistry
{
public:
	FBuildFunctionRegistry();
	~FBuildFunctionRegistry();

	const IBuildFunction* FindFunction(FStringView Function) const;
	FGuid FindFunctionVersion(FStringView Function) const;
	void IterateFunctionVersions(TFunctionRef<void(FStringView Function, const FGuid& Version)> Visitor) const;

private:
	void OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature);
	void OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature);

	void AddFunctionNoLock(IBuildFunctionFactory* Factory);
	void RemoveFunction(IBuildFunctionFactory* Factory);

private:
	mutable FRWLock Lock;
	TMap<FString, IBuildFunctionFactory*> Functions;
};

FBuildFunctionRegistry::FBuildFunctionRegistry()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	for (IBuildFunctionFactory* Factory : ModularFeatures.GetModularFeatureImplementations<IBuildFunctionFactory>(IBuildFunctionFactory::GetFeatureName()))
	{
		AddFunctionNoLock(Factory);
	}
	ModularFeatures.OnModularFeatureRegistered().AddRaw(this, &FBuildFunctionRegistry::OnModularFeatureRegistered);
	ModularFeatures.OnModularFeatureUnregistered().AddRaw(this, &FBuildFunctionRegistry::OnModularFeatureUnregistered);
}

FBuildFunctionRegistry::~FBuildFunctionRegistry()
{
	IModularFeatures& ModularFeatures = IModularFeatures::Get();
	ModularFeatures.OnModularFeatureUnregistered().RemoveAll(this);
	ModularFeatures.OnModularFeatureRegistered().RemoveAll(this);
}

void FBuildFunctionRegistry::OnModularFeatureRegistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (Type == IBuildFunctionFactory::GetFeatureName())
	{
		FWriteScopeLock WriteLock(Lock);
		AddFunctionNoLock(static_cast<IBuildFunctionFactory*>(ModularFeature));
	}
}

void FBuildFunctionRegistry::OnModularFeatureUnregistered(const FName& Type, IModularFeature* ModularFeature)
{
	if (Type == IBuildFunctionFactory::GetFeatureName())
	{
		RemoveFunction(static_cast<IBuildFunctionFactory*>(ModularFeature));
	}
}

void FBuildFunctionRegistry::AddFunctionNoLock(IBuildFunctionFactory* Factory)
{
	const IBuildFunction& Function = Factory->GetFunction();
	const FStringView FunctionName = Function.GetName();
	const uint32 FunctionHash = GetTypeHash(FunctionName);
	UE_CLOG(!Function.GetVersion().IsValid(), LogDerivedDataBuild, Error,
		TEXT("Version of zero is not allowed in build function with the name %.*s."),
		FunctionName.Len(), FunctionName.GetData());
	UE_CLOG(Functions.FindByHash(FunctionHash, FunctionName), LogDerivedDataBuild, Error,
		TEXT("More than one build function has been registered with the name %.*s."),
		FunctionName.Len(), FunctionName.GetData());
	Functions.EmplaceByHash(FunctionHash, FunctionName, Factory);
}

void FBuildFunctionRegistry::RemoveFunction(IBuildFunctionFactory* Factory)
{
	const FStringView Function = Factory->GetFunction().GetName();
	const uint32 FunctionHash = GetTypeHash(Function);
	FWriteScopeLock WriteLock(Lock);
	Functions.RemoveByHash(FunctionHash, Function);
}

const IBuildFunction* FBuildFunctionRegistry::FindFunction(FStringView Function) const
{
	FReadScopeLock ReadLock(Lock);
	if (IBuildFunctionFactory* const* Factory = Functions.FindByHash(GetTypeHash(Function), Function))
	{
		return &(**Factory).GetFunction();
	}
	return nullptr;
}

FGuid FBuildFunctionRegistry::FindFunctionVersion(FStringView Function) const
{
	FReadScopeLock ReadLock(Lock);
	if (IBuildFunctionFactory* const* Factory = Functions.FindByHash(GetTypeHash(Function), Function))
	{
		return (**Factory).GetFunction().GetVersion();
	}
	return FGuid();
}

void FBuildFunctionRegistry::IterateFunctionVersions(TFunctionRef<void(FStringView Function, const FGuid& Version)> Visitor) const
{
	TArray<TPair<FStringView, FGuid>> FunctionVersions;
	{
		FReadScopeLock ReadLock(Lock);
		FunctionVersions.Reserve(Functions.Num());
		for (const TPair<FStringView, IBuildFunctionFactory*>& Function : Functions)
		{
			FunctionVersions.Emplace(Function.Key, Function.Value->GetFunction().GetVersion());
		}
	}

	for (const TPair<FStringView, FGuid>& Function : FunctionVersions)
	{
		Visitor(Function.Key, Function.Value);
	}
}

IBuildFunctionRegistry* CreateBuildFunctionRegistry()
{
	return new FBuildFunctionRegistry();
}

} // UE::DerivedData::Private
