// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information about a group of nodes
	/// </summary>
	public class CreateAggregateRequest
	{
		/// <summary>
		/// Name of the aggregate
		/// </summary>
		[Required]
		public string Name { get; set; }

		/// <summary>
		/// Nodes which must be part of the job for the aggregate to be valid
		/// </summary>
		[Required]
		public List<string> Nodes { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateAggregateRequest()
		{
			Name = null!;
			Nodes = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this aggregate</param>
		/// <param name="Nodes">Nodes which must be part of the job for the aggregate to be shown</param>
		public CreateAggregateRequest(string Name, List<string> Nodes)
		{
			this.Name = Name;
			this.Nodes = Nodes;
		}
	}

	/// <summary>
	/// Response from creating a new aggregate
	/// </summary>
	public class CreateAggregateResponse
	{
		/// <summary>
		/// Index of the first aggregate that was added
		/// </summary>
		public int FirstIndex { get; set; }
	}
}
