// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"


class UObject;
class UOptimusNodeGraph;


enum class EOptimusGlobalNotifyType
{
	GraphAdded,				/// A new graph has been added (Subject == UOptimusNodeGraph)
	GraphRemoved,			/// A graph is about to be removed (Subject == UOptimusNodeGraph)
	GraphIndexChanged,		/// A graph's index been changed (Subject == UOptimusNodeGraph)
	GraphRenamed,			/// A graph's name has been changed (Subject == UOptimusNodeGraph)

	ResourceAdded,			/// A resource has been added (Subject == UOptimusResourceDescription)
	ResourceRemoved,		/// A resource is about to be removed (Subject == UOptimusResourceDescription)
	ResourceIndexChanged,	/// A resource's index has changed (Subject == UOptimusResourceDescription)
	ResourceRenamed,		/// A resource has been renamed (Subject == UOptimusResourceDescription)
	ResourceTypeChanged,	/// A resource's type has been changed (Subject == UOptimusResourceDescription)

	VariableAdded,			/// A variable has been added (Subject == UOptimusVariableDescription)
	VariableRemoved,		/// A variable is about to be removed (Subject == UOptimusVariableDescription)
	VariableIndexChanged,	/// A variable's index has changed (Subject == UOptimusVariableDescription)
	VariableRenamed,		/// A variable has been renamed (Subject == UOptimusVariableDescription)
	VariabelTypeChanged,	/// A variable's type has been changed (Subject == UOptimusVariableDescription)
};

// A delegate for subscribing / reacting to Optimus global notifications.
DECLARE_MULTICAST_DELEGATE_TwoParams(FOptimusGlobalNotifyDelegate, EOptimusGlobalNotifyType /* type */, UObject* /* subject */);


enum class EOptimusGraphNotifyType
{
	NodeAdded,				/// A new node has been added (Subject == UOptimusNode)
	NodeRemoved,			/// A node has been removed (Subject == UOptimusNode)
	NodeDisplayNameChanged,	/// A node's display name has changed (Subject == UOptimusNode)
	NodePositionChanged,	/// A node's position in the graph has changed (Subject == UOptimusNode)

	LinkAdded,				/// A link between nodes has been added (Subject == UOptimusNodeLink)
	LinkRemoved,			/// A link between nodes has been removed (Subject == UOptimusNodeLink)

	PinValueChanged,		/// A pin on a node has had its value changed (Subject = UOptimusNodePin)
};

// A delegate for subscribing / reacting to Optimus graph local notifications.
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOptimusGraphNotifyDelegate, EOptimusGraphNotifyType /* type */, UOptimusNodeGraph*/*graph */, UObject* /* subject */);
