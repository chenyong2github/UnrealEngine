// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Serialization;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.Horde.Compute
{
	/// <summary>
	/// Stores information about a directory in an action's workspace
	/// </summary>
	public class Requirements
	{
		/// <summary>
		/// Condition string to be evaluated against the machine spec, eg. cpu-cores >= 10 && ram.mb >= 200 && pool == 'worker'
		/// </summary>
		[CbField("c")]
		public Utf8String Condition { get; set; }

		/// <summary>
		/// Resources used by the process
		/// </summary>
		[CbField("r")]
		public Dictionary<Utf8String, int> Resources { get; set; } = new Dictionary<Utf8String, int>();

		/// <summary>
		/// Whether we require exclusive access to the device
		/// </summary>
		[CbField("e")]
		public bool Exclusive { get; set; }
	}
}
