// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/RenderPagePropsSourceFactoryLocal.h"
#include "RenderPage/RenderPagePropsSource.h"


URenderPagePropsSourceBase* UE::RenderPages::Private::FRenderPagePropsSourceFactoryLocal::CreateInstance(UObject* Outer, UObject* PropsSourceOrigin)
{
	URenderPagePropsSourceLocal* PropsSource = NewObject<URenderPagePropsSourceLocal>(Outer);
	PropsSource->SetSourceOrigin(PropsSourceOrigin);
	return PropsSource;
}
