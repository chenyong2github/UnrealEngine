// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FDragDropEvent;

/** @return the content being dragged if it matched the 'OperationType'; invalid Ptr otherwise. */
template<typename OperationType>
TSharedPtr<OperationType> FDragDropEvent::GetOperationAs() const
{
	if (Content.IsValid() && Content->IsOfType<OperationType>())
	{
		return Content->CastTo<OperationType>();
	}
	else
	{
		return TSharedPtr<OperationType>();
	}
}