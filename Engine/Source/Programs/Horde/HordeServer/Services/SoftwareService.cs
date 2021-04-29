// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Collections;
using HordeServer.Models;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Security.Claims;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

using SoftwareChannelId = HordeServer.Utilities.StringId<HordeServer.Services.SoftwareVersionsSingleton>;

namespace HordeServer.Services
{
	/// <summary>
	/// Singleton document used to track
	/// </summary>
	[SingletonDocument("5f455039d97900f2b6c735a9")]
	class SoftwareVersionsSingleton : SingletonBase
	{
		/// <summary>
		/// The next version number
		/// </summary>
		public int NextVersion { get; set; }

		/// <summary>
		/// Maps from a label name to a software version index
		/// </summary>
		public Dictionary<SoftwareChannelId, int> Channels { get; set; } = new Dictionary<SoftwareChannelId, int>();

		/// <summary>
		/// Maps from a version nubmer to a document id
		/// </summary>
		public Dictionary<int, ObjectId> Versions { get; set; } = new Dictionary<int, ObjectId>();
	}

	/// <summary>
	/// Wrapper for a collection of software revisions. 
	/// </summary>
	public class SoftwareService
	{
		/// <summary>
		/// Cached software information
		/// </summary>
		class CachedSoftware
		{
			/// <summary>
			/// The software id
			/// </summary>
			public readonly ObjectId? Id;

			/// <summary>
			/// Time at which the 
			/// </summary>
			readonly Stopwatch Timer;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Id">The unique software id</param>
			public CachedSoftware(ObjectId? Id)
			{
				this.Id = Id;
				this.Timer = Stopwatch.StartNew();
			}

			/// <summary>
			/// Whether the cached software is valid
			/// </summary>
			public TimeSpan ElapsedTime
			{
				get { return Timer.Elapsed; }
			}
		}

		/// <summary>
		/// Collection of software documents
		/// </summary>
		ISoftwareCollection SoftwareCollection;

		/// <summary>
		/// The cached software instance
		/// </summary>
		CachedSoftware? CachedDefaultSoftware;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="SoftwareCollection">The software collection</param>
		public SoftwareService(ISoftwareCollection SoftwareCollection)
		{
			this.SoftwareCollection = SoftwareCollection;
		}

		/// <summary>
		/// Create a new software revision
		/// </summary>
		/// <param name="ZipStream">The input data stream. This should be a zip archive containing the HordeAgent executable.</param>
		/// <param name="UserName">Name of the user uploading this file</param>
		/// <param name="bDefault">Whether this software should immediately be made the default</param>
		/// <returns>Unique id for the file</returns>
		public async Task<ObjectId> CreateSoftwareAsync(System.IO.Stream ZipStream, string? UserName, bool bDefault)
		{
			// Read the whole thing into memory
			using MemoryStream Stream = new MemoryStream();
			await ZipStream.CopyToAsync(Stream);

			// Insert the new document
			ISoftware App = await SoftwareCollection.AddAsync(Stream.ToArray(), UserName, bDefault);
			return App.Id;
		}

		/// <summary>
		/// Upload a software archive to the backend
		/// </summary>
		/// <param name="Id">Unique id of the software to update</param>
		/// <param name="User">Name of the current user</param>
		/// <param name="Default">Whether the software should be the default</param>
		/// <returns>Async task</returns>
		public Task<bool> UpdateSoftwareAsync(ObjectId Id, string? User, bool Default)
		{
			return SoftwareCollection.UpdateAsync(Id, User, Default);
		}

		/// <summary>
		/// Finds all software archives matching a set of criteria
		/// </summary>
		/// <param name="UploadedByUser">The user that uploaded the software</param>
		/// <param name="MadeDefaultByUser">The user that made the software the default</param>
		/// <param name="MadeDefault">Whether the software was made default</param>
		/// <param name="Offset">Index of the first result to return</param>
		/// <param name="Count">Number of results to return</param>
		public Task<List<ISoftware>> FindSoftwareAsync(string? UploadedByUser, string? MadeDefaultByUser, bool? MadeDefault, int Offset, int Count)
		{
			return SoftwareCollection.FindAsync(UploadedByUser, MadeDefaultByUser, MadeDefault, Offset, Count);
		}

		/// <summary>
		/// Gets a single software archive
		/// </summary>
		/// <param name="Id">Unique id of the file to retrieve</param>
		/// <param name="bIncludeData">Whether to include the data</param>
		public Task<ISoftware?> GetSoftwareAsync(ObjectId Id, bool bIncludeData)
		{
			return SoftwareCollection.GetAsync(Id, bIncludeData);
		}

		/// <summary>
		/// Finds the current default software
		/// </summary>
		/// <param name="bIncludeData">Whether to include the data</param>
		/// <returns>The current default software</returns>
		public async Task<ISoftware?> GetDefaultSoftwareAsync(bool bIncludeData)
		{
			ISoftware? DefaultSoftware = await SoftwareCollection.GetDefaultAsync(bIncludeData);
			CachedDefaultSoftware = new CachedSoftware(DefaultSoftware?.Id);
			return DefaultSoftware;
		}

		/// <summary>
		/// Gets a cached value for the default software
		/// </summary>
		/// <returns>Unique id of the default software</returns>
		public async Task<string?> GetCachedDefaultSoftwareIdAsync()
		{
			CachedSoftware? CachedDefaultSoftwareCopy = CachedDefaultSoftware;
			if (CachedDefaultSoftwareCopy != null && CachedDefaultSoftwareCopy.ElapsedTime < TimeSpan.FromMinutes(1.0))
			{
				return CachedDefaultSoftwareCopy.Id?.ToString();
			}

			ISoftware? DefaultSoftware = await GetDefaultSoftwareAsync(false);
			if (DefaultSoftware != null)
			{
				return DefaultSoftware.Id.ToString();
			}

			return null;
		}

		/// <summary>
		/// Removes a software archive 
		/// </summary>
		/// <param name="Id">Unique id of the software to delete</param>
		public Task<bool> DeleteSoftwareAsync(ObjectId Id)
		{
			return SoftwareCollection.DeleteAsync(Id);
		}
	}
}
