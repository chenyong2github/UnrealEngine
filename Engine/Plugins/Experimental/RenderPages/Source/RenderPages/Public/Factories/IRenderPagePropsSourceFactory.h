// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


class URenderPagePropsSourceBase;


namespace UE::RenderPages
{
	/**
	 * The base class for the factory classes that will create URenderPagePropsSourceBase instances.
	 */
	class RENDERPAGES_API IRenderPagePropsSourceFactory
	{
	public:
		virtual ~IRenderPagePropsSourceFactory() = default;
		virtual URenderPagePropsSourceBase* CreateInstance(UObject* Outer, UObject* PropsSourceOrigin) { return nullptr; }
	};
}
