// Copyright Epic Games, Inc. All Rights Reserved.

/*==============================================================================
	AGXDynamicRHIModule.h: AGX Dynamic RHI Module Class.
==============================================================================*/

#pragma once


//------------------------------------------------------------------------------

#pragma mark - AGX Dynamic RHI Module Class


class FAGXDynamicRHIModule : public IDynamicRHIModule
{
public:
	virtual bool IsSupported() override final;

	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override final;
};
