// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/IRenderPagePropsSourceFactory.h"


namespace UE::RenderPages::Private
{
	/**
	 * The factory class for URenderPagePropsSourceLocal.
	 */
	class RENDERPAGES_API FRenderPagePropsSourceFactoryLocal final : public IRenderPagePropsSourceFactory
	{
	public:
		virtual URenderPagePropsSourceBase* CreateInstance(UObject* Outer, UObject* PropsSourceOrigin) override;
	};
}
