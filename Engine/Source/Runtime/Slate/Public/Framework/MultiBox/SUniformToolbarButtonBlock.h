// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Framework/MultiBox/SToolBarButtonBlock.h"

struct FButtonArgs;
/**
 * Horizontal Button Row MultiBlock
 */
class SLATE_API FUniformToolbarButtonBlock
	: public FToolBarButtonBlock
{

public:

	/**
	 * Constructor
	 *
	 * @param	ButtonArgs The Parameters object which will provide the data to initialize the button
	 */
	FUniformToolbarButtonBlock( FButtonArgs ButtonArgs);

	
	/**
	 * Allocates a widget for this type of MultiBlock.  Override this in derived classes.
	 *
	 * @return  MultiBlock widget object
	 */
	 virtual TSharedRef< class IMultiBlockBaseWidget > ConstructWidget() const override;

};

class SLATE_API SUniformToolbarButtonBlock
	: public SToolBarButtonBlock
{
	
};

