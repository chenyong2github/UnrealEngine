// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson;
using MongoDB.Bson.Serialization.Attributes;
using MongoDB.Driver;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using ProjectId = StringId<IProject>;

	/// <summary>
	/// Specifies a category of streams to display on the dashboard
	/// </summary>
	public class StreamCategory
	{
		/// <summary>
		/// Name of this group
		/// </summary>
		[BsonRequired]
		public string Name { get; set; }

		/// <summary>
		/// Row to display this category on
		/// </summary>
		public int Row { get; set; }

		/// <summary>
		/// Whether to show this category on the nav menu
		/// </summary>
		public bool ShowOnNavMenu { get; set; } = true;

		/// <summary>
		/// List of stream name patterns to include for this project
		/// </summary>
		public List<string> IncludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Patterns for stream names to be excluded
		/// </summary>
		public List<string> ExcludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Private constructor
		/// </summary>
		private StreamCategory()
		{
			Name = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name">Name of this group</param>
		public StreamCategory(string Name)
		{
			this.Name = Name;
		}

		/// <summary>
		/// Constructs a category from a request object
		/// </summary>
		/// <param name="Request">The request object</param>
		public StreamCategory(CreateProjectCategoryRequest Request)
		{
			this.Name = Request.Name;
			this.Row = Request.Row;
			this.ShowOnNavMenu = Request.ShowOnNavMenu;
			this.IncludePatterns = Request.IncludePatterns;
			this.ExcludePatterns = Request.ExcludePatterns;
		}

		/// <summary>
		/// Tests if a given name matches a pattern
		/// </summary>
		/// <param name="Name">The name to test</param>
		/// <param name="Pattern">The pattern to match against</param>
		/// <returns>True if the pattern matches, false otherwise</returns>
		public static bool MatchPattern(string Name, string Pattern)
		{
			return Regex.IsMatch(Name, Pattern);
		}
	}

	/// <summary>
	/// Represents a project
	/// </summary>
	public interface IProject
	{
		/// <summary>
		/// Identifier for the project.
		/// </summary>
		public ProjectId Id { get; }

		/// <summary>
		/// Name of the project
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Path to the config file used to configure this project
		/// </summary>
		public string? ConfigPath { get; }

		/// <summary>
		/// Revision of the config file used to configure this project
		/// </summary>
		public string? ConfigRevision { get; }

		/// <summary>
		/// Order to display on the dashboard
		/// </summary>
		public int Order { get; }

		/// <summary>
		/// The ACL for this object
		/// </summary>
		public Acl? Acl { get; }

		/// <summary>
		/// List of stream categories for this project. Controls how streams are displayed on the dashboard.
		/// </summary>
		public IReadOnlyList<StreamCategory> Categories { get; }
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
		/// <param name="Project">The project instance</param>
		/// <param name="bIncludeStreams">Whether to include streams in the response</param>
		/// <param name="bIncludeCategories">Whether to include categories in the response</param>
		/// <param name="Streams">The list of streams</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		/// <returns>Response instance</returns>
		public static GetProjectResponse ToResponse(this IProject Project, bool bIncludeStreams, bool bIncludeCategories, List<IStream>? Streams, bool bIncludeAcl)
		{
			List<GetProjectStreamResponse>? StreamResponses = null;
			if(bIncludeStreams)
			{
				StreamResponses = Streams!.ConvertAll(x => new GetProjectStreamResponse(x.Id.ToString(), x.Name));
			}

			List<GetProjectCategoryResponse>? CategoryResponses = null;
			if (bIncludeCategories)
			{
				CategoryResponses = Project.Categories.ConvertAll(x => new GetProjectCategoryResponse(x));
				if (Streams != null)
				{
					foreach (IStream Stream in Streams)
					{
						GetProjectCategoryResponse? CategoryResponse = CategoryResponses.FirstOrDefault(x => MatchCategory(Stream.Name, x));
						if(CategoryResponse == null)
						{
							int Row = (CategoryResponses.Count > 0) ? CategoryResponses.Max(x => x.Row) : 0;
							if (CategoryResponses.Count(x => x.Row == Row) >= 3)
							{
								Row++;
							}

							StreamCategory OtherCategory = new StreamCategory("Other");
							OtherCategory.Row = Row;
							OtherCategory.IncludePatterns.Add(".*");

							CategoryResponse = new GetProjectCategoryResponse(OtherCategory);
							CategoryResponses.Add(CategoryResponse);
						}
						CategoryResponse.Streams!.Add(Stream.Id.ToString());
					}
				}
			}

			GetAclResponse? AclResponse = (bIncludeAcl && Project.Acl != null) ? new GetAclResponse(Project.Acl) : null;
			return new GetProjectResponse(Project.Id.ToString(), Project.Name, Project.Order, StreamResponses, CategoryResponses, AclResponse);
		}

		/// <summary>
		/// Tests if a category response matches a given stream name
		/// </summary>
		/// <param name="Name">The stream name</param>
		/// <param name="Category">The category response</param>
		/// <returns>True if the category matches</returns>
		static bool MatchCategory(string Name, GetProjectCategoryResponse Category)
		{
			if (Category.IncludePatterns.Any(x => StreamCategory.MatchPattern(Name, x)))
			{
				if (!Category.ExcludePatterns.Any(x => StreamCategory.MatchPattern(Name, x)))
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
