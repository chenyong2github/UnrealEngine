// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;
using System.Collections.Generic;

namespace HordeServer.Models
{
	/// <summary>
	/// A service account used for authenticating during server-to-server communication
	/// For example between Robomerge and Horde.
	/// </summary>
	public interface IServiceAccount
	{
		/// <summary>
		/// Unique id for this session
		/// </summary>
		public ObjectId Id { get; }
		
		/// <summary>
		/// Secret token used for identifying calls made as the service account
		/// </summary>
		public string SecretToken { get; }
		
		/// <summary>
		/// Claims
		/// </summary>
		public IReadOnlyDictionary<string, string> Claims { get; }

		/// <summary>
		/// If the service account is active
		/// </summary>
		public bool Enabled { get; }
		
		/// <summary>
		/// Description of the service account (who is it for, is there an owner etc)
		/// </summary>
		public string Description { get; }
	}
}
