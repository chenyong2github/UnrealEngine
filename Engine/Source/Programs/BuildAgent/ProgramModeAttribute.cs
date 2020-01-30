// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BuildAgent
{
	/// <summary>
	/// Attribute used to specify names of program modes, and help text
	/// </summary>
	class ProgramModeAttribute : Attribute
	{
		/// <summary>
		/// Name of the program mode, which needs to be specified on the command line to execute it
		/// </summary>
		public string Name;

		/// <summary>
		/// Short description for the mode. Will be displayed in the help text.
		/// </summary>
		public string Description;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of the mode</param>
		/// <param name="Description">Short description for display in the help text</param>
		public ProgramModeAttribute(string Name, string Description)
		{
			this.Name = Name;
			this.Description = Description;
		}
	}
}
