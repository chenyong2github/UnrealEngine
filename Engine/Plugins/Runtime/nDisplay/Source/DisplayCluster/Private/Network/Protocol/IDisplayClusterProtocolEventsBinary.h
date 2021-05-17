// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

struct FDisplayClusterClusterEventBinary;


/**
 * Binary cluster events protocol.
 */
class IDisplayClusterProtocolEventsBinary
{
public:
	virtual ~IDisplayClusterProtocolEventsBinary() = default;

public:
	virtual void EmitClusterEventBinary(const FDisplayClusterClusterEventBinary& Event) = 0;
};
