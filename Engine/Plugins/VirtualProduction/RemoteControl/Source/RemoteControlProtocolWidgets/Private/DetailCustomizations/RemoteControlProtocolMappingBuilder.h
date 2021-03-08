// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

#include "IDetailCustomNodeBuilder.h"
#include "IDetailChildrenBuilder.h"

/** Custom struct for each group of arguments in the function editing details */
class FRemoteControlProtocolMappingBuilder : public IDetailCustomNodeBuilder, public TSharedFromThis<FRemoteControlProtocolMappingBuilder>
{
protected:

	// IDetailCustomNodeBuilder interface
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRegenerateChildren) override;
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual void Tick(float DeltaTime) override {}
	virtual bool RequiresTick() const override { return false; }
	virtual FName GetName() const override { return FName(TEXT("Lightmaps")); }
	virtual bool InitiallyCollapsed() const override { return false; }
};
