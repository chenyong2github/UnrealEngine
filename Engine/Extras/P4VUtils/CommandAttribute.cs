// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace P4VUtils
{
	public enum CommandCategory
	{
		/// <summary>
		/// Commands that help with common but simple operations
		/// </summary>
		Root,

		/// <summary>
		/// commands that help with submitting files/changelists
		/// </summary>
		Submission,

		/// <summary>
		/// Commands that help with common but simple operations
		/// </summary>
		Toolbox,

		/// <summary>
		/// complex commands to facilitate integrations/backout
		/// </summary>
		Integrate,

		/// <summary>
		/// local build and horde preflights
		/// </summary>
		Horde
	}

	/// <summary>
	/// Attribute used to define a class as a Command to hook into P4V
	/// 
	/// Containing attributes to define the cli name of the command, the categorization of the command, and the order it appears in the UI.
	/// </summary>
	[AttributeUsage(AttributeTargets.Class)]
	public sealed class CommandAttribute : System.Attribute
	{
		/// <summary>
		/// Command name, use to reference the command via command line args
		/// </summary>
		public string CommandName { get; internal set; } = null!;

		/// <summary>
		/// Category to organize into UI Elements. 
		/// </summary>
		public CommandCategory Category { get; internal set; }

		/// <summary>
		/// Used to preserve previous order.
		/// 
		/// Defaults to int.MaxValue to ensure any new items appear at the end of the sub menus.
		/// </summary>
		public int Order { get; internal set; }

		public CommandAttribute(string commandName, CommandCategory category, int order = int.MaxValue)
		{
			CommandName = commandName;
			Category = category;
			Order = order;
		}
	}
}
