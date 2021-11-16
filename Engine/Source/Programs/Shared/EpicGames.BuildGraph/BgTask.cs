// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.Text;
using System.Xml;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// A task invocation
	/// </summary>
	public class BgTask
	{
		/// <summary>
		/// Line number in a source file that this task was declared. Optional; used for log messages.
		/// </summary>
		public BgScriptLocation Location { get; }

		/// <summary>
		/// Name of the task
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Arguments for the task
		/// </summary>
		public Dictionary<string, string> Arguments { get; } = new Dictionary<string, string>();

		/// <summary>
		/// Constructor
		/// </summary>
		public BgTask(BgScriptLocation Location, string Name)
		{
			this.Location = Location;
			this.Name = Name;
		}

		/// <summary>
		/// Write to an xml file
		/// </summary>
		/// <param name="Writer"></param>
		public void Write(XmlWriter Writer)
		{
			Writer.WriteStartElement(Name);
			foreach (KeyValuePair<string, string> Argument in Arguments)
			{
				Writer.WriteAttributeString(Argument.Key, Argument.Value);
			}
			Writer.WriteEndElement();
		}
	}
}
