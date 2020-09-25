// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorView.h"

/**
 * The Interface for controll the Output Mapping Мiew
 */
class IDisplayClusterConfiguratorViewOutputMapping
	: public IDisplayClusterConfiguratorView
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnShowWindowInfo, bool);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnShowWindowCornerImage, bool);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnShowOutsideViewports, bool);

	using FOnShowWindowInfoDelegate = FOnShowWindowInfo::FDelegate;
	using FOnShowWindowCornerImageDelegate = FOnShowWindowCornerImage::FDelegate;
	using FOnShowOutsideViewportsDelegate = FOnShowOutsideViewports::FDelegate;

public:
	virtual bool IsRulerVisible() const = 0;

	virtual bool IsShowWindowInfo() const = 0;

	virtual bool IsShowWindowCornerImage() const = 0;

	virtual bool IsShowOutsideViewports() const = 0;

	virtual FDelegateHandle RegisterOnShowWindowInfo(const FOnShowWindowInfoDelegate& Delegate) = 0;

	virtual void UnregisterOnShowWindowInfo(FDelegateHandle DelegateHandle) = 0;

	virtual FDelegateHandle RegisterOnShowWindowCornerImage(const FOnShowWindowCornerImageDelegate& Delegate) = 0;

	virtual void UnregisterOnShowWindowCornerImage(FDelegateHandle DelegateHandle) = 0;

	virtual FDelegateHandle RegisterOnShowOutsideViewports(const FOnShowOutsideViewportsDelegate& Delegate) = 0;

	virtual void UnregisterOnShowOutsideViewports(FDelegateHandle DelegateHandle) = 0;
};
