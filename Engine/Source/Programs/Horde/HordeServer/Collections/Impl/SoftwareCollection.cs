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

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of template documents
	/// </summary>
	public class SoftwareCollection : ISoftwareCollection
	{
		/// <summary>
		/// Information about agent software
		/// </summary>
		class SoftwareDocument : ISoftware
		{
			[BsonRequired, BsonId]
			public ObjectId Id { get; set; }

			public byte[]? Data { get; set; }
			public string? UploadedByUser { get; set; }
			public DateTime UploadedAtTime { get; set; }
			public string? MadeDefaultByUser { get; set; }
			public DateTime? MadeDefaultAtTime { get; set; }

			[BsonConstructor]
			private SoftwareDocument()
			{
			}

			public SoftwareDocument(ObjectId Id, byte[] Data, string? UserName, bool bDefault)
			{
				this.Id = Id;
				this.Data = Data;
				this.UploadedAtTime = DateTime.UtcNow;
				this.UploadedByUser = UserName;

				if (bDefault)
				{
					this.MadeDefaultAtTime = DateTime.UtcNow;
				}
			}

		}

		/// <summary>
		/// Template documents
		/// </summary>
		IMongoCollection<SoftwareDocument> Collection;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public SoftwareCollection(DatabaseService DatabaseService)
		{
			Collection = DatabaseService.GetCollection<SoftwareDocument>("Software");
			if (!DatabaseService.ReadOnlyMode)
			{
				Collection.Indexes.CreateOne(new CreateIndexModel<SoftwareDocument>(Builders<SoftwareDocument>.IndexKeys.Descending(x => x.MadeDefaultAtTime!)));
			}
		}

		/// <inheritdoc/>
		public async Task<ISoftware> AddAsync(byte[] Data, string? User, bool Default)
		{
			ObjectId Id = ObjectId.GenerateNewId();
			byte[] NewData = UpdateVersion(Data, Id.ToString());

			SoftwareDocument Document = new SoftwareDocument(Id, NewData, User, Default);
			await Collection.InsertOneAsync(Document);
			return Document;
		}

		/// <summary>
		/// Updates the client id within the archive in the data array
		/// </summary>
		/// <param name="Data">Data for the zip archive</param>
		/// <param name="Version">The new version identifier</param>
		/// <returns>New agent app data</returns>
		public static byte[] UpdateVersion(byte[] Data, string Version)
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
							Document["Horde"]["Version"] = Version;

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
		public async Task<bool> UpdateAsync(ObjectId Id, string? User, bool Default)
		{
			UpdateDefinition<SoftwareDocument> Update;
			if (Default)
			{
				Update = Builders<SoftwareDocument>.Update.Set(x => x.MadeDefaultAtTime, DateTime.UtcNow).Set(x => x.MadeDefaultByUser, User);
			}
			else
			{
				Update = Builders<SoftwareDocument>.Update.Set(x => x.MadeDefaultAtTime, null);
			}

			UpdateResult Result = await Collection.UpdateOneAsync(x => x.Id == Id, Update);
			return Result.ModifiedCount > 0;
		}

		/// <inheritdoc/>
		public async Task<List<ISoftware>> FindAsync(string? UploadedByUser, string? MadeDefaultByUser, bool? MadeDefault, int Offset, int Count)
		{
			FilterDefinitionBuilder<SoftwareDocument> FilterBuilder = Builders<SoftwareDocument>.Filter;

			FilterDefinition<SoftwareDocument> Filter = FilterDefinition<SoftwareDocument>.Empty;
			if (UploadedByUser != null)
			{
				Filter &= FilterBuilder.Eq(x => x.UploadedByUser, UploadedByUser);
			}
			if (MadeDefaultByUser != null)
			{
				Filter &= FilterBuilder.Eq(x => x.MadeDefaultByUser, MadeDefaultByUser);
			}
			if (MadeDefault.HasValue)
			{
				if (MadeDefault.Value)
				{
					Filter &= FilterBuilder.Ne(x => x.MadeDefaultAtTime, null);
				}
				else
				{
					Filter &= FilterBuilder.Eq(x => x.MadeDefaultAtTime, null);
				}
			}

			List<SoftwareDocument> Results = await Collection.Find(Filter).SortByDescending(x => x.MadeDefaultAtTime).Skip(Offset).Limit(Count).ToListAsync();
			return Results.ConvertAll<ISoftware>(x => x);
		}

		/// <inheritdoc/>
		public async Task<ISoftware?> GetAsync(ObjectId Id, bool bIncludeData)
		{
			IFindFluent<SoftwareDocument, SoftwareDocument> Matches = Collection.Find(x => x.Id == Id);
			if (!bIncludeData)
			{
				Matches = Matches.Project<SoftwareDocument>(Builders<SoftwareDocument>.Projection.Exclude(x => x.Data));
			}
			return await Matches.FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<ISoftware?> GetDefaultAsync(bool bIncludeData)
		{
			IFindFluent<SoftwareDocument, SoftwareDocument> Matches = Collection.Find(FilterDefinition<SoftwareDocument>.Empty).SortByDescending(x => x.MadeDefaultAtTime);
			if (!bIncludeData)
			{
				Matches = Matches.Project<SoftwareDocument>(Builders<SoftwareDocument>.Projection.Exclude(x => x.Data));
			}

			SoftwareDocument? DefaultSoftware = await Matches.FirstOrDefaultAsync();
			if (DefaultSoftware != null && !DefaultSoftware.MadeDefaultAtTime.HasValue)
			{
				DefaultSoftware = null;
			}

			return DefaultSoftware;
		}

		/// <inheritdoc/>
		public async Task<bool> DeleteAsync(ObjectId Id)
		{
			DeleteResult Result = await Collection.DeleteOneAsync(x => x.Id == Id);
			return Result.DeletedCount > 0;
		}
	}
}
