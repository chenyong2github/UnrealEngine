// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRHIStagingBuffer.h: AGX RHI Staging Buffer Class.
=============================================================================*/

#pragma once

class FAGXRHIStagingBuffer final : public FRHIStagingBuffer
{
	friend class FAGXRHICommandContext;

public:
	FAGXRHIStagingBuffer();

	virtual ~FAGXRHIStagingBuffer() final override;

	/**
	 * Returns the pointer to read the buffer.
	 *
	 * There is no actual locking, the buffer is always shared.  If this is not
	 * fenced correctly, it will not have the expected data.
	 */
	virtual void* Lock(uint32 Offset, uint32 NumBytes) final override;

	virtual void Unlock() final override;

private:
	FAGXBuffer ShadowBuffer;
};
