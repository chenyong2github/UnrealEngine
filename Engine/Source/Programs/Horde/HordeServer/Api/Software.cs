// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Parameters for creating a new software archive
	/// </summary>
	public class CreateSoftwareRequest
	{
		/// <summary>
		/// Whether this software should be the default
		/// </summary>
		public bool Default { get; set; }
	}

	/// <summary>
	/// Information about a client version
	/// </summary>
	public class CreateSoftwareResponse
	{
		/// <summary>
		/// The software id
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Identifier for the software</param>
		public CreateSoftwareResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Parameters for updating a software archive
	/// </summary>
	public class UpdateSoftwareRequest
	{
		/// <summary>
		/// Whether this software should be the default
		/// </summary>
		public bool Default { get; set; }
	}

	/// <summary>
	/// Information about an uploaded software archive
	/// </summary>
	public class GetSoftwareResponse
	{
		/// <summary>
		/// Unique id for this enty
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the user that created this software
		/// </summary>
		public string? UploadedByUser { get; set; }

		/// <summary>
		/// Time at which the client was created
		/// </summary>
		public DateTime UploadedAtTime { get; set; }

		/// <summary>
		/// Name of the user that created this software
		/// </summary>
		public string? MadeDefaultByUser { get; set; }

		/// <summary>
		/// Time at which the client was made default.
		/// </summary>
		public DateTime? MadeDefaultAtTime { get; set; }

		/// <summary>
		/// Default constructor for serialization
		/// </summary>
		private GetSoftwareResponse()
		{
			Id = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Software">The object to construct from</param>
		public GetSoftwareResponse(ISoftware Software)
		{
			this.Id = Software.Id.ToString();
			this.UploadedByUser = Software.UploadedByUser;
			this.UploadedAtTime = Software.UploadedAtTime;
			this.MadeDefaultByUser = Software.MadeDefaultByUser;
			this.MadeDefaultAtTime = Software.MadeDefaultAtTime;
		}
	}
}
