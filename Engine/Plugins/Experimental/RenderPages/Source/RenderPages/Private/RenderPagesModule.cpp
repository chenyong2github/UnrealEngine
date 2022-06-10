// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPagesModule.h"
#include "RenderPage/RenderPageManager.h"
#include "RenderPage/RenderPagePropsSource.h"
#include "Factories/RenderPagePropsSourceFactoryLocal.h"
#include "Factories/RenderPagePropsSourceFactoryRemoteControl.h"

#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "RenderPagesModule"


void UE::RenderPages::Private::FRenderPagesModule::StartupModule()
{
	IFileManager::Get().DeleteDirectory(*FRenderPageManager::TmpRenderedFramesPath, false, true);
	RegisterPropsSourceFactories();
	CreateManager();
}

void UE::RenderPages::Private::FRenderPagesModule::ShutdownModule()
{
	RemoveManager();
	UnregisterPropsSourceFactories();
}


void UE::RenderPages::Private::FRenderPagesModule::CreateManager()
{
	Manager = MakeUnique<FRenderPageManager>();
}

void UE::RenderPages::Private::FRenderPagesModule::RemoveManager()
{
	Manager.Reset();
}

UE::RenderPages::FRenderPageManager& UE::RenderPages::Private::FRenderPagesModule::GetManager() const
{
	check(Manager.IsValid());
	return *Manager;
}


void UE::RenderPages::Private::FRenderPagesModule::RegisterPropsSourceFactories()
{
	RegisterPropsSourceFactory(ERenderPagePropsSourceType::Local, MakeShared<FRenderPagePropsSourceFactoryLocal>());
	RegisterPropsSourceFactory(ERenderPagePropsSourceType::RemoteControl, MakeShared<FRenderPagePropsSourceFactoryRemoteControl>());
}

void UE::RenderPages::Private::FRenderPagesModule::UnregisterPropsSourceFactories()
{
	UnregisterPropsSourceFactory(ERenderPagePropsSourceType::Local);
	UnregisterPropsSourceFactory(ERenderPagePropsSourceType::RemoteControl);
}

void UE::RenderPages::Private::FRenderPagesModule::RegisterPropsSourceFactory(const ERenderPagePropsSourceType PropsSourceType, const TSharedPtr<IRenderPagePropsSourceFactory>& InFactory)
{
	PropsSourceFactories.Add(PropsSourceType, InFactory);
}

void UE::RenderPages::Private::FRenderPagesModule::UnregisterPropsSourceFactory(const ERenderPagePropsSourceType PropsSourceType)
{
	PropsSourceFactories.Remove(PropsSourceType);
}

URenderPagePropsSourceBase* UE::RenderPages::Private::FRenderPagesModule::CreatePropsSource(UObject* Outer, ERenderPagePropsSourceType PropsSourceType, UObject* PropsSourceOrigin)
{
	TSharedPtr<IRenderPagePropsSourceFactory>* FactoryPtr = PropsSourceFactories.Find(PropsSourceType);
	if (!FactoryPtr)
	{
		return nullptr;
	}

	TSharedPtr<IRenderPagePropsSourceFactory> Factory = *FactoryPtr;
	if (!Factory)
	{
		return nullptr;
	}

	return Factory->CreateInstance(Outer, PropsSourceOrigin);
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(UE::RenderPages::Private::FRenderPagesModule, RenderPages)
