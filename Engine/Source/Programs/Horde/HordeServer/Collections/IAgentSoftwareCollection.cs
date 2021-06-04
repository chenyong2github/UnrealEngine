// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Models;
using HordeServer.Services;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

namespace HordeServer.Collections
{
	/// <summary>
	/// Interface for a collection of template documents
	/// </summary>
	public interface IAgentSoftwareCollection
	{
		/// <summary>
		/// Adds a new software revision
		/// </summary>
		/// <param name="Version">The version number</param>
		/// <param name="Data">Zip file containing the new software</param>
		/// <returns>New software instance</returns>
		Task<bool> AddAsync(string Version, byte[] Data);

		/// <summary>
		/// Tests whether a given version exists
		/// </summary>
		/// <param name="Version">Version of the software</param>
		/// <returns>True if it exists, false otherwise</returns>
		Task<bool> ExistsAsync(string Version);

		/// <summary>
		/// Removes a software archive 
		/// </summary>
		/// <param name="Version">Version of the software to delete</param>
		Task<bool> RemoveAsync(string Version);

		/// <summary>
		/// Downloads software of a given revision
		/// </summary>
		/// <param name="Version">Version of the software</param>
		/// <returns>Data for the given software</returns>
		Task<byte[]?> GetAsync(string Version);
	}
}
