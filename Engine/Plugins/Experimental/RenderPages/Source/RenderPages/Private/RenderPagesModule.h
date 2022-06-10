// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IRenderPagesModule.h"
#include "Factories/IRenderPagePropsSourceFactory.h"


namespace UE::RenderPages::Private
{
	/**
	 * The implementation of the IRenderPagesModule interface.
	 */
	class FRenderPagesModule : public IRenderPagesModule
	{
	public:
		//~ Begin IModuleInterface interface
		virtual void StartupModule() override;
		virtual void ShutdownModule() override;
		//~ End IModuleInterface interface

		//~ Begin IRenderPagesModule interface
		virtual FRenderPageManager& GetManager() const override;

		virtual URenderPagePropsSourceBase* CreatePropsSource(UObject* Outer, ERenderPagePropsSourceType PropsSourceType, UObject* PropsSourceOrigin) override;
		virtual const TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceFactory>>& GetPropsSourceFactories() const override { return PropsSourceFactories; }
		//~ End IRenderPagesModule interface

	private:
		void CreateManager();
		void RemoveManager();

		void RegisterPropsSourceFactories();
		void UnregisterPropsSourceFactories();
		void RegisterPropsSourceFactory(const ERenderPagePropsSourceType PropsSourceType, const TSharedPtr<IRenderPagePropsSourceFactory>& InFactory);
		void UnregisterPropsSourceFactory(const ERenderPagePropsSourceType PropsSourceType);

	private:
		TUniquePtr<FRenderPageManager> Manager;
		TMap<ERenderPagePropsSourceType, TSharedPtr<IRenderPagePropsSourceFactory>> PropsSourceFactories;
	};
}
