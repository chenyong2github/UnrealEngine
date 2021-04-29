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
	public interface ISoftwareCollection
	{
		/// <summary>
		/// Adds a new software revision
		/// </summary>
		/// <param name="Data">Zip file containing the new software</param>
		/// <param name="User">User that uploaded the software</param>
		/// <param name="Default">Whether to make the software the default</param>
		/// <returns>New software instance</returns>
		Task<ISoftware> AddAsync(byte[] Data, string? User, bool Default);

		/// <summary>
		/// Upload a software archive to the backend
		/// </summary>
		/// <param name="Id">Unique id of the software to update</param>
		/// <param name="User">Name of the current user</param>
		/// <param name="Default">Whether the software should be the default</param>
		/// <returns>Async task</returns>
		Task<bool> UpdateAsync(ObjectId Id, string? User, bool Default);

		/// <summary>
		/// Finds all software archives matching a set of criteria
		/// </summary>
		/// <param name="UploadedByUser">The user that uploaded the software</param>
		/// <param name="MadeDefaultByUser">The user that made the software the default</param>
		/// <param name="MadeDefault">Whether the software was made default</param>
		/// <param name="Offset">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		Task<List<ISoftware>> FindAsync(string? UploadedByUser, string? MadeDefaultByUser, bool? MadeDefault, int Offset, int Count);

		/// <summary>
		/// Gets a single software archive
		/// </summary>
		/// <param name="Id">Unique id of the file to retrieve</param>
		/// <param name="bIncludeData">Whether to include the data</param>
		Task<ISoftware?> GetAsync(ObjectId Id, bool bIncludeData);

		/// <summary>
		/// Finds the current default software
		/// </summary>
		/// <param name="bIncludeData">Whether to include the data</param>
		/// <returns>The current default software</returns>
		Task<ISoftware?> GetDefaultAsync(bool bIncludeData);

		/// <summary>
		/// Removes a software archive 
		/// </summary>
		/// <param name="Id">Unique id of the software to delete</param>
		Task<bool> DeleteAsync(ObjectId Id);
	}
}
