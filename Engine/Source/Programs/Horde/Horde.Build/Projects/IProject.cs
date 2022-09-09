// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using Horde.Build.Acls;
using Horde.Build.Streams;
using Horde.Build.Utilities;
using MongoDB.Driver;

namespace Horde.Build.Projects
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Represents a project
	/// </summary>
	public interface IProject
	{
		/// <summary>
		/// Identifier for the project.
		/// </summary>
		ProjectId Id { get; }

		/// <summary>
		/// Name of the project
		/// </summary>
		string Name { get; }

		/// <summary>
		/// Revision of the config file used to configure this project
		/// </summary>
		string? ConfigRevision { get; }

		/// <summary>
		/// Order to display on the dashboard
		/// </summary>
		public int Order { get; }

		/// <summary>
		/// Configuration settings for the stream
		/// </summary>
		ProjectConfig Config { get; }

		/// <summary>
		/// The ACL for this object
		/// </summary>
		Acl? Acl { get; }
	}
	
	/// <summary>
	/// Interface for a document containing a project logo
	/// </summary>
	public interface IProjectLogo
	{
		/// <summary>
		/// The project id
		/// </summary>
		public ProjectId Id { get; }

		/// <summary>
		/// Path to the logo
		/// </summary>
		public string Path { get; }

		/// <summary>
		/// Revision of the file
		/// </summary>
		public string Revision { get; }

		/// <summary>
		/// Mime type for the image
		/// </summary>
		public string MimeType { get; }

		/// <summary>
		/// Image data
		/// </summary>
		public byte[] Data { get; }
	}

	/// <summary>
	/// Extension methods for projects
	/// </summary>
	public static class ProjectExtensions
	{
		/// <summary>
		/// Converts this object to a public response
		/// </summary>
		/// <param name="project">The project instance</param>
		/// <param name="bIncludeStreams">Whether to include streams in the response</param>
		/// <param name="bIncludeCategories">Whether to include categories in the response</param>
		/// <param name="streams">The list of streams</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		/// <returns>Response instance</returns>
		public static GetProjectResponse ToResponse(this IProject project, bool bIncludeStreams, bool bIncludeCategories, List<IStream>? streams, bool bIncludeAcl)
		{
			List<GetProjectStreamResponse>? streamResponses = null;
			if(bIncludeStreams)
			{
				streamResponses = streams!.ConvertAll(x => new GetProjectStreamResponse(x.Id.ToString(), x.Name));
			}

			List<GetProjectCategoryResponse>? categoryResponses = null;
			if (bIncludeCategories)
			{
				categoryResponses = project.Config.Categories.ConvertAll(x => new GetProjectCategoryResponse(x));
				if (streams != null)
				{
					foreach (IStream stream in streams)
					{
						GetProjectCategoryResponse? categoryResponse = categoryResponses.FirstOrDefault(x => MatchCategory(stream.Name, x));
						if(categoryResponse == null)
						{
							int row = (categoryResponses.Count > 0) ? categoryResponses.Max(x => x.Row) : 0;
							if (categoryResponses.Count(x => x.Row == row) >= 3)
							{
								row++;
							}

							ProjectCategoryConfig otherCategory = new ProjectCategoryConfig();
							otherCategory.Name = "Other";
							otherCategory.Row = row;
							otherCategory.IncludePatterns.Add(".*");

							categoryResponse = new GetProjectCategoryResponse(otherCategory);
							categoryResponses.Add(categoryResponse);
						}
						categoryResponse.Streams!.Add(stream.Id.ToString());
					}
				}
			}

			GetAclResponse? aclResponse = (bIncludeAcl && project.Acl != null) ? new GetAclResponse(project.Acl) : null;
			return new GetProjectResponse(project.Id.ToString(), project.Name, project.Order, streamResponses, categoryResponses, aclResponse);
		}

		/// <summary>
		/// Tests if a category response matches a given stream name
		/// </summary>
		/// <param name="name">The stream name</param>
		/// <param name="category">The category response</param>
		/// <returns>True if the category matches</returns>
		static bool MatchCategory(string name, GetProjectCategoryResponse category)
		{
			if (category.IncludePatterns.Any(x => Regex.IsMatch(name, x)))
			{
				if (!category.ExcludePatterns.Any(x => Regex.IsMatch(name, x)))
				{
					return true;
				}
			}
			return false;
		}
	}

	/// <summary>
	/// Projection of a project definition to just include permissions info
	/// </summary>
	public interface IProjectPermissions
	{
		/// <summary>
		/// ACL for the project
		/// </summary>
		public Acl? Acl { get; }
	}
}
