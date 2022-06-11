// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RenderPagePropsSourceFactoryRemoteControl.h"
#include "RenderPage/RenderPagePropsSource.h"


URenderPagePropsSourceBase* UE::RenderPages::Private::FRenderPagePropsSourceFactoryRemoteControl::CreateInstance(UObject* Outer, UObject* PropsSourceOrigin)
{
	URenderPagePropsSourceRemoteControl* PropsSource = NewObject<URenderPagePropsSourceRemoteControl>(Outer);
	PropsSource->SetSourceOrigin(PropsSourceOrigin);
	return PropsSource;
}
