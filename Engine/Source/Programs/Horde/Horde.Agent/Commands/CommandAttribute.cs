// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace HordeAgent.Commands
{
	/// <summary>
	/// Attribute used to specify names of program modes, and help text
	/// </summary>
	class CommandAttribute : Attribute
	{
		/// <summary>
		/// Names for this command
		/// </summary>
		public string[] Names;

		/// <summary>
		/// Short description for the mode. Will be displayed in the help text.
		/// </summary>
		public string Description;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the mode</param>
		/// <param name="Description">Short description for display in the help text</param>
		public CommandAttribute(string Name, string Description)
		{
			this.Names = new string[] { Name };
			this.Description = Description;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Category">Category for this command</param>
		/// <param name="Name">Name of the mode</param>
		/// <param name="Description">Short description for display in the help text</param>
		public CommandAttribute(string Category, string Name, string Description)
		{
			this.Names = new string[] { Category, Name };
			this.Description = Description;
		}
	}
}
