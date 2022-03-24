// Copyright Epic Games, Inc. All Rights Reserved.

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
		public string Name { get; }

		/// <summary>
		/// Description for this option
		/// </summary>
		public string Description { get; }

		/// <summary>
		/// Default value for this option
		/// </summary>
		public string DefaultValue { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="name">The name of this option</param>
		/// <param name="description">Description of the option, for display on help pages</param>
		/// <param name="defaultValue">Default value for the option</param>
		public BgOption(string name, string description, string defaultValue)
		{
			Name = name;
			Description = description;
			DefaultValue = defaultValue;
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
