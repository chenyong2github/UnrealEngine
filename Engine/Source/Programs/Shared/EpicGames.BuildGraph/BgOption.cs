// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Represents a graph option. These are expanded during preprocessing, but are retained in order to display help messages.
	/// </summary>
	public class BgOption
	{
		/// <summary>
		/// Name of this option
		/// </summary>
		public string Name;

		/// <summary>
		/// Description for this option
		/// </summary>
		public string Description;

		/// <summary>
		/// Default value for this option
		/// </summary>
		public string DefaultValue;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">The name of this option</param>
		/// <param name="Description">Description of the option, for display on help pages</param>
		/// <param name="DefaultValue">Default value for the option</param>
		public BgOption(string Name, string Description, string DefaultValue)
		{
			this.Name = Name;
			this.Description = Description;
			this.DefaultValue = DefaultValue;
		}

		/// <summary>
		/// Returns a name of this option for debugging
		/// </summary>
		/// <returns>Name of the option</returns>
		public override string ToString()
		{
			return Name;
		}
	}
}
