// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Models;
using HordeServer.Services;
using HordeServer.Utilities;
using Microsoft.Extensions.Options;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Security.Claims;
using System.Threading.Tasks;

using ProjectId = HordeServer.Utilities.StringId<HordeServer.Models.IProject>;

namespace HordeServer.Collections.Impl
{
	/// <summary>
	/// Wraps functionality for manipulating projects
	/// </summary>
	public class ProjectCollection : IProjectCollection
	{
		/// <summary>
		/// Represents a project
		/// </summary>
		class ProjectDocument : IProject
		{
			public const int DefaultOrder = 128;

			[BsonRequired, BsonId]
			public ProjectId Id { get; set; }

			[BsonRequired]
			public string Name { get; set; }

			public string? Revision { get; set; }

			public int Order { get; set; } = DefaultOrder;
			public Acl? Acl { get; set; }
			public List<StreamCategory> Categories { get; set; } = new List<StreamCategory>();
			public Dictionary<string, string> Properties { get; set; } = new Dictionary<string, string>(StringComparer.Ordinal);

			[BsonIgnoreIfDefault, BsonDefaultValue(false)]
			public bool Deleted { get; set; }

			IReadOnlyList<StreamCategory> IProject.Categories => Categories;
			IReadOnlyDictionary<string, string> IProject.Properties => Properties;

			[BsonConstructor]
			private ProjectDocument()
			{
				Name = null!;
			}

			public ProjectDocument(ProjectId Id, string Name)
			{
				this.Id = Id;
				this.Name = Name;
			}
		}

		/// <summary>
		/// Logo for a project
		/// </summary>
		class ProjectLogoDocument : IProjectLogo
		{
			public ProjectId Id { get; set; }

			public string Revision { get; set; } = String.Empty;
			public string MimeType { get; set; } = String.Empty;

			public byte[] Data { get; set; } = Array.Empty<byte>();
		}

		/// <summary>
		/// Projection of a project definition to just include permissions info
		/// </summary>
		[SuppressMessage("Design", "CA1812: Class is never instantiated")]
		class ProjectPermissions : IProjectPermissions
		{
			/// <summary>
			/// ACL for the project
			/// </summary>
			public Acl? Acl { get; set; }

			/// <summary>
			/// Projection to extract the permissions info from the project
			/// </summary>
			public static readonly ProjectionDefinition<ProjectDocument> Projection = Builders<ProjectDocument>.Projection.Include(x => x.Acl);
		}

		/// <summary>
		/// Collection of project documents
		/// </summary>
		IMongoCollection<ProjectDocument> Projects;

		/// <summary>
		/// Collection of project logo documents
		/// </summary>
		IMongoCollection<ProjectLogoDocument> ProjectLogos;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="DatabaseService">The database service instance</param>
		public ProjectCollection(DatabaseService DatabaseService)
		{
			Projects = DatabaseService.GetCollection<ProjectDocument>("Projects");
			ProjectLogos = DatabaseService.GetCollection<ProjectLogoDocument>("ProjectLogos");
		}

		/// <inheritdoc/>
		public async Task<IProject?> AddOrUpdateAsync(ProjectId Id, string Revision, int Order, ProjectConfig Config)
		{
			ProjectDocument NewProject = new ProjectDocument(Id, Config.Name);
			NewProject.Revision = Revision;
			NewProject.Order = Order;
			NewProject.Categories = Config.Categories.ConvertAll(x => new StreamCategory(x));
			NewProject.Acl = Acl.Merge(new Acl(), Config.Acl);

			await Projects.FindOneAndReplaceAsync<ProjectDocument>(x => x.Id == Id, NewProject, new FindOneAndReplaceOptions<ProjectDocument> { IsUpsert = true });
			return NewProject;
		}

		/// <inheritdoc/>
		public async Task<IProject?> TryAddAsync(ProjectId Id, string Name, int? Order, List<StreamCategory>? Categories, Dictionary<string, string>? Properties)
		{
			ProjectDocument NewProject = new ProjectDocument(Id, Name);
			if (Order != null)
			{
				NewProject.Order = Order.Value;
			}
			if (Categories != null)
			{
				NewProject.Categories = Categories;
			}
			if (Properties != null)
			{
				NewProject.Properties = new Dictionary<string, string>(Properties, NewProject.Properties.Comparer);
			}

			// Attempt to insert the stream, but fail gracefully if it already exists
			try
			{
				await Projects.InsertOneAsync(NewProject);
				return NewProject;
			}
			catch (MongoWriteException Ex)
			{
				if (Ex.WriteError.Category == ServerErrorCategory.DuplicateKey)
				{
					return null;
				}
				else
				{
					throw;
				}
			}
		}

		/// <inheritdoc/>
		public async Task UpdateAsync(ProjectId ProjectId, string? NewName, int? NewOrder, List<StreamCategory>? NewCategories, Dictionary<string, string>? NewProperties, Acl? NewAcl)
		{
			UpdateDefinitionBuilder<ProjectDocument> UpdateBuilder = Builders<ProjectDocument>.Update;

			List<UpdateDefinition<ProjectDocument>> Updates = new List<UpdateDefinition<ProjectDocument>>();
			if (NewName != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Name, NewName));
			}
			if (NewOrder != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Order, NewOrder.Value));
			}
			if (NewCategories != null)
			{
				Updates.Add(UpdateBuilder.Set(x => x.Categories, NewCategories));
			}
			if (NewProperties != null)
			{
				foreach (KeyValuePair<string, string> Pair in NewProperties)
				{
					if (Pair.Value == null)
					{
						Updates.Add(UpdateBuilder.Unset(x => x.Properties[Pair.Key]));
					}
					else
					{
						Updates.Add(UpdateBuilder.Set(x => x.Properties[Pair.Key], Pair.Value));
					}
				}
			}
			if (NewAcl != null)
			{
				Updates.Add(Acl.CreateUpdate<ProjectDocument>(x => x.Acl!, NewAcl));
			}

			if (Updates.Count > 0)
			{
				await Projects.FindOneAndUpdateAsync<ProjectDocument>(x => x.Id == ProjectId, UpdateBuilder.Combine(Updates));
			}
		}

		/// <inheritdoc/>
		public async Task<List<IProject>> FindAllAsync()
		{
			List<ProjectDocument> Results = await Projects.Find(x => !x.Deleted).ToListAsync();
			return Results.OrderBy(x => x.Order).ThenBy(x => x.Name).Select<ProjectDocument, IProject>(x => x).ToList();
		}

		/// <inheritdoc/>
		public async Task<IProject?> GetAsync(ProjectId ProjectId)
		{
			return await Projects.Find<ProjectDocument>(x => x.Id == ProjectId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task<IProjectLogo?> GetLogoAsync(ProjectId ProjectId)
		{
			return await ProjectLogos.Find(x => x.Id == ProjectId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task SetLogoAsync(ProjectId ProjectId, string Revision, string MimeType, byte[] Data)
		{
			ProjectLogoDocument Logo = new ProjectLogoDocument();
			Logo.Id = ProjectId;
			Logo.Revision = Revision;
			Logo.MimeType = MimeType;
			Logo.Data = Data;
			await ProjectLogos.ReplaceOneAsync(x => x.Id == ProjectId, Logo, new ReplaceOptions { IsUpsert = true });
		}

		/// <inheritdoc/>
		public async Task<IProjectPermissions?> GetPermissionsAsync(ProjectId ProjectId)
		{
			return await Projects.Find<ProjectDocument>(x => x.Id == ProjectId).Project<ProjectPermissions>(ProjectPermissions.Projection).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task DeleteAsync(ProjectId ProjectId)
		{
			await Projects.UpdateOneAsync(x => x.Id == ProjectId, Builders<ProjectDocument>.Update.Set(x => x.Deleted, true));
		}
	}
}
