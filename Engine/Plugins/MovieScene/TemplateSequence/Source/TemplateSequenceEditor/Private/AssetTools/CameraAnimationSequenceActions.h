// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TemplateSequenceActions.h"

class FCameraAnimationSequenceActions : public FTemplateSequenceActions
{
public:
    
    FCameraAnimationSequenceActions(const TSharedRef<ISlateStyle>& InStyle);

    virtual FText GetName() const override;
    virtual UClass* GetSupportedClass() const override;

protected:

    virtual void InitializeToolkitParams(FTemplateSequenceToolkitParams& ToolkitParams) const override;
};

