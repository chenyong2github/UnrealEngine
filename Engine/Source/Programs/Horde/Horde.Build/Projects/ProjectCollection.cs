// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;
using Horde.Build.Server;
using Horde.Build.Utilities;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using MongoDB.Driver;

namespace Horde.Build.Projects
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Wraps functionality for manipulating projects
	/// </summary>
	public class ProjectCollection : IProjectCollection
	{
		/// <summary>
		/// Logo for a project
		/// </summary>
		class ProjectLogoDocument : IProjectLogo
		{
			public ProjectId Id { get; set; }

			public string Path { get; set; } = String.Empty;
			public string Revision { get; set; } = String.Empty;
			public string MimeType { get; set; } = String.Empty;

			public byte[] Data { get; set; } = Array.Empty<byte>();
		}

		readonly IMongoCollection<ProjectLogoDocument> _projectLogos;

		/// <summary>
		/// Constructor
		/// </summary>
		public ProjectCollection(MongoService mongoService)
		{
			_projectLogos = mongoService.GetCollection<ProjectLogoDocument>("ProjectLogos");
		}

		/// <inheritdoc/>
		public async Task<IProjectLogo?> GetLogoAsync(ProjectId projectId)
		{
			return await _projectLogos.Find(x => x.Id == projectId).FirstOrDefaultAsync();
		}

		/// <inheritdoc/>
		public async Task SetLogoAsync(ProjectId projectId, string logoPath, string logoRevision, string mimeType, byte[] data)
		{
			ProjectLogoDocument logo = new ProjectLogoDocument();
			logo.Id = projectId;
			logo.Path = logoPath;
			logo.Revision = logoRevision;
			logo.MimeType = mimeType;
			logo.Data = data;
			await _projectLogos.ReplaceOneAsync(x => x.Id == projectId, logo, new ReplaceOptions { IsUpsert = true });
		}
	}
}
