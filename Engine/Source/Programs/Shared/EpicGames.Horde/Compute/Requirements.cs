// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Horde.Common;
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
		public Condition? Condition { get; set; }

		/// <summary>
		/// Resources used by the process
		/// </summary>
		[CbField("r")]
		public Dictionary<string, int> Resources { get; set; } = new Dictionary<string, int>();

		/// <summary>
		/// Whether we require exclusive access to the device
		/// </summary>
		[CbField("e")]
		public bool Exclusive { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public Requirements()
		{
		}

		/// <summary>
		/// Construct a requirements object with a condition
		/// </summary>
		/// <param name="Condition"></param>
		public Requirements(Condition? Condition)
		{
			this.Condition = Condition;
		}
	}
}
