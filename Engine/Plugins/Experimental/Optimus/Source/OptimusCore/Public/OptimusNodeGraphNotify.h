// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

class UOptimusNodeGraph;

enum class EOptimusNodeGraphNotifyType
{
	GraphAdded,				/// A new graph has been added (Subject == Nothing)
	GraphRemoved,			/// A graph has been removed (Subject == Nothing)
	GraphIndexChanged,		/// A graph's index been changed (Subject == Nothing)
	GraphNameChanged,		/// A graph's name has been changed (Subject == Nothing)

	NodeAdded,				/// A new node has been added (Subject == UOptimusNode)
	NodeRemoved,			/// A node has been removed (Subject == UOptimusNode)

	NodeLinkAdded,			/// A link between nodes has been added (Subject == UOptimusNodeLink)
	NodeLinkRemoved,		/// A link between nodes has been removed (Subject == UOptimusNodeLink)

	NodeDisplayNameChanged,	/// A node's display name has changed (Subject == UOptimusNode)
	NodePositionChanged,	/// A node's position in the graph has changed (Subject == UOptimusNode)

	PinValueChanged,		/// A pin on a node has had its value changed (Subject = UOptimusNodePin)
};

// A delegate for subscribing / reacting to graph modifications.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOptimusNodeGraphEvent, EOptimusNodeGraphNotifyType /* type */, UOptimusNodeGraph* /* graph */, UObject* /* subject */);
