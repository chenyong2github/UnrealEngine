// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeCommon;
using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Caching.Memory;
using MongoDB.Bson;
using MongoDB.Bson.Serialization;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Collection of template documents
	/// </summary>
	public sealed class TemplateCollection : ITemplateCollection, IDisposable
	{
		/// <summary>
		/// Document describing a job template. These objects are considered immutable once created and uniquely referenced by hash, in order to de-duplicate across all job runs.
		/// </summary>
		class TemplateDocument : ITemplate
		{
			[BsonRequired, BsonId]
			public ContentHash Id { get; private set; } = ContentHash.Empty;

			[BsonRequired]
			public string Name { get; private set; }

			public Priority? Priority { get; private set; }
			public bool AllowPreflights { get; set; } = true;
			public string? InitialAgentType { get; set; }

			[BsonIgnoreIfNull]
			public string? SubmitNewChange { get; set; }

			public List<string> Arguments { get; private set; } = new List<string>();
			public List<Parameter> Parameters { get; private set; } = new List<Parameter>();

			IReadOnlyList<string> ITemplate.Arguments => Arguments;
			IReadOnlyList<Parameter> ITemplate.Parameters => Parameters;

			[BsonConstructor]
			private TemplateDocument()
			{
				Name = null!;
			}

			public TemplateDocument(string Name, Priority? Priority, bool bAllowPreflights, string? InitialAgentType, string? SubmitNewChange, List<string>? Arguments, List<Parameter>? Parameters)
			{
				this.Name = Name;
				this.Priority = Priority;
				this.AllowPreflights = bAllowPreflights;
				this.InitialAgentType = InitialAgentType;
				this.SubmitNewChange = SubmitNewChange;
				this.Arguments = Arguments ?? new List<string>();
				this.Parameters = Parameters ?? new List<Parameter>();

				// Compute the hash once all other fields have been set
				this.Id = ContentHash.SHA1(BsonExtensionMethods.ToBson(this));
			}
		}

		/// <summary>
		/// Template documents
		/// </summary>
		IMongoCollection<TemplateDocument> Templates;

		/// <summary>
		/// Cache of template documents
		/// </summary>
		MemoryCache TemplateCache;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service singleton</param>
		public TemplateCollection(DatabaseService DatabaseService)
		{
			// Ensure discriminator cannot be registered twice (throws exception). Can otherwise happen during unit tests.
			if (BsonSerializer.LookupDiscriminatorConvention(typeof(JobsTabColumn)) == null)
			{
				BsonSerializer.RegisterDiscriminatorConvention(typeof(JobsTabColumn), new DefaultDiscriminatorConvention(typeof(JobsTabColumn), typeof(JobsTabLabelColumn)));	
			}
			
			Templates = DatabaseService.GetCollection<TemplateDocument>("Templates");

			MemoryCacheOptions Options = new MemoryCacheOptions();
			TemplateCache = new MemoryCache(Options);
		}

		/// <inheritdoc/>
		public void Dispose()
		{
			TemplateCache.Dispose();
		}

		/// <inheritdoc/>
		public async Task<ITemplate> AddAsync(string Name, Priority? Priority, bool bAllowPreflights, string? InitialAgentType, string? SubmitNewChange, List<string>? Arguments, List<Parameter>? Parameters)
		{
			TemplateDocument Template = new TemplateDocument(Name, Priority, bAllowPreflights, InitialAgentType, SubmitNewChange, Arguments, Parameters);
			if (await GetAsync(Template.Id) == null)
			{
				await Templates.ReplaceOneAsync(x => x.Id == Template.Id, Template, new ReplaceOptions { IsUpsert = true });
			}
			return Template;
		}

		/// <inheritdoc/>
		public async Task<List<ITemplate>> FindAllAsync()
		{
			List<TemplateDocument> Results = await Templates.Find(FilterDefinition<TemplateDocument>.Empty).ToListAsync();
			return Results.ConvertAll<ITemplate>(x => x);
		}

		/// <inheritdoc/>
		public async Task<ITemplate?> GetAsync(ContentHash TemplateId)
		{
			object? Result;
			if (TemplateCache.TryGetValue(TemplateId, out Result))
			{
				return (ITemplate?)Result;
			}

			ITemplate? Template = await Templates.Find<TemplateDocument>(x => x.Id == TemplateId).FirstOrDefaultAsync();
			if (Template != null)
			{
				using (ICacheEntry Entry = TemplateCache.CreateEntry(TemplateId))
				{
					Entry.SetSlidingExpiration(TimeSpan.FromMinutes(30.0));
					Entry.SetValue(Template);
				}
			}

			return Template;
		}
	}
}
