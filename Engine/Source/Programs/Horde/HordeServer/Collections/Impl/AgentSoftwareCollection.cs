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

using AgentSoftwareVersion = HordeServer.Utilities.StringId<HordeServer.Collections.IAgentSoftwareCollection>;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of agent software documents
	/// </summary>
	public class AgentSoftwareCollection : IAgentSoftwareCollection
	{
		/// <summary>
		/// Information about agent software
		/// </summary>
		class AgentSoftwareDocument
		{
			[BsonId]
			public AgentSoftwareVersion Id { get; set; }
			public byte[] Data { get; set; } = null!;

			[BsonConstructor]
			private AgentSoftwareDocument()
			{
				this.Data = null!;
			}

			public AgentSoftwareDocument(AgentSoftwareVersion Id, byte[] Data)
			{
				this.Id = Id;
				this.Data = Data;
			}
		}

		/// <summary>
		/// Template documents
		/// </summary>
		IMongoCollection<AgentSoftwareDocument> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public AgentSoftwareCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<AgentSoftwareDocument>("AgentSoftware");
		}

		/// <inheritdoc/>
		public async Task AddAsync(AgentSoftwareVersion Version, byte[] Data)
		{
			byte[] NewData = UpdateVersion(Data, Version);

			AgentSoftwareDocument Document = new AgentSoftwareDocument(Version, NewData);
			await Collection.InsertOneAsync(Document);
		}

		/// <summary>
		/// Updates the client id within the archive in the data array
		/// </summary>
		/// <param name="Data">Data for the zip archive</param>
		/// <param name="Version">The new version identifier</param>
		/// <returns>New agent app data</returns>
		public static byte[] UpdateVersion(byte[] Data, AgentSoftwareVersion Version)
		{
			bool bWrittenClientId = false;

			MemoryStream OutputStream = new MemoryStream();
			using (ZipArchive OutputArchive = new ZipArchive(OutputStream, ZipArchiveMode.Create, true))
			{
				MemoryStream InputStream = new MemoryStream(Data);
				using (ZipArchive InputArchive = new ZipArchive(InputStream, ZipArchiveMode.Read, true))
				{
					foreach (ZipArchiveEntry InputEntry in InputArchive.Entries)
					{
						ZipArchiveEntry OutputEntry = OutputArchive.CreateEntry(InputEntry.FullName);

						using System.IO.Stream InputEntryStream = InputEntry.Open();
						using System.IO.Stream OutputEntryStream = OutputEntry.Open();

						if (InputEntry.FullName.Equals("appsettings.json", StringComparison.OrdinalIgnoreCase))
						{
							using MemoryStream MemoryStream = new MemoryStream();
							InputEntryStream.CopyTo(MemoryStream);

							Dictionary<string, Dictionary<string, object>> Document = JsonSerializer.Deserialize<Dictionary<string, Dictionary<string, object>>>(MemoryStream.ToArray());
							Document["Horde"]["Version"] = Version.ToString();

							using Utf8JsonWriter Writer = new Utf8JsonWriter(OutputEntryStream, new JsonWriterOptions { Indented = true });
							JsonSerializer.Serialize<Dictionary<string, Dictionary<string, object>>>(Writer, Document, new JsonSerializerOptions { WriteIndented = true });

							bWrittenClientId = true;
						}
						else
						{
							InputEntryStream.CopyTo(OutputEntryStream);
						}
					}
				}
			}

			if (!bWrittenClientId)
			{
				throw new InvalidDataException("Missing appsettings.json file from zip archive");
			}

			return OutputStream.ToArray();
		}

		/// <inheritdoc/>
		public async Task<bool> RemoveAsync(AgentSoftwareVersion Version)
		{
			DeleteResult Result = await Collection.DeleteOneAsync(x => x.Id == Version);
			return Result.DeletedCount > 0;
		}

		/// <inheritdoc/>
		public async Task<byte[]?> GetAsync(AgentSoftwareVersion Version)
		{
			IFindFluent<AgentSoftwareDocument, AgentSoftwareDocument> Query = Collection.Find(x => x.Id == Version);
			AgentSoftwareDocument? Software = await Query.FirstOrDefaultAsync();
			return Software?.Data;
		}
	}
}
