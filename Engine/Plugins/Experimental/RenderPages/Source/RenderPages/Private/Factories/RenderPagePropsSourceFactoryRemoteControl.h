// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/IRenderPagePropsSourceFactory.h"


namespace UE::RenderPages::Private
{
	/**
	 * The factory class for URenderPagePropsSourceRemoteControl.
	 */
	class RENDERPAGES_API FRenderPagePropsSourceFactoryRemoteControl final : public IRenderPagePropsSourceFactory
	{
	public:
		virtual URenderPagePropsSourceBase* CreateInstance(UObject* Outer, UObject* PropsSourceOrigin) override;
	};
}
