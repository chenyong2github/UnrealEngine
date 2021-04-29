// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Models;
using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Api
{
	/// <summary>
	/// Information about a category to display for a stream
	/// </summary>
	public class CreateProjectCategoryRequest
	{
		/// <summary>
		/// Name of this category
		/// </summary>
		[Required]
		public string Name { get; set; }

		/// <summary>
		/// Index of the row to display this category on
		/// </summary>
		public int Row { get; set; }

		/// <summary>
		/// Whether to show this category on the nav menu
		/// </summary>
		public bool ShowOnNavMenu { get; set; }

		/// <summary>
		/// Patterns for stream names to include
		/// </summary>
		public List<string> IncludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Patterns for stream names to exclude
		/// </summary>
		public List<string> ExcludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateProjectCategoryRequest()
		{
			Name = null!;
		}
	}

	/// <summary>
	/// Parameters to create a new project
	/// </summary>
	public class CreateProjectRequest
	{
		/// <summary>
		/// Name for the new project
		/// </summary>
		[Required]
		public string Name { get; set; } = null!;

		/// <summary>
		/// Order to display this project on the dashboard
		/// </summary>
		public int? Order { get; set; }

		/// <summary>
		/// Categories to include in this project
		/// </summary>
		public List<CreateProjectCategoryRequest>? Categories { get; set; }

		/// <summary>
		/// Properties for the new project
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }
	}

	/// <summary>
	/// Response from creating a new project
	/// </summary>
	public class CreateProjectResponse
	{
		/// <summary>
		/// Unique id for the new project
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private CreateProjectResponse()
		{
			Id = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id for the new project</param>
		public CreateProjectResponse(string Id)
		{
			this.Id = Id;
		}
	}

	/// <summary>
	/// Parameters to update a project
	/// </summary>
	public class UpdateProjectRequest
	{
		/// <summary>
		/// Optional new name for the project
		/// </summary>
		public string? Name { get; set; }

		/// <summary>
		/// Order to display this project on the dashboard
		/// </summary>
		public int? Order { get; set; }

		/// <summary>
		/// New set of categories for the request
		/// </summary>
		public List<CreateProjectCategoryRequest>? Categories { get; set; }

		/// <summary>
		/// Properties to update for the project. Properties set to null will be removed.
		/// </summary>
		public Dictionary<string, string>? Properties { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Information about a stream within a project
	/// </summary>
	public class GetProjectStreamResponse
	{
		/// <summary>
		/// The stream id
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The stream name
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">The unique stream id</param>
		/// <param name="Name">The stream name</param>
		public GetProjectStreamResponse(string Id, string Name)
		{
			this.Id = Id;
			this.Name = Name;
		}
	}

	/// <summary>
	/// Information about a category to display for a stream
	/// </summary>
	public class GetProjectCategoryResponse
	{
		/// <summary>
		/// Heading for this column
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Index of the row to display this category on
		/// </summary>
		public int Row { get; set; }

		/// <summary>
		/// Whether to show this category on the nav menu
		/// </summary>
		public bool ShowOnNavMenu { get; set; }

		/// <summary>
		/// Patterns for stream names to include
		/// </summary>
		public List<string> IncludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Patterns for stream names to exclude
		/// </summary>
		public List<string> ExcludePatterns { get; set; } = new List<string>();

		/// <summary>
		/// Streams to include in this category
		/// </summary>
		public List<string> Streams { get; set; } = new List<string>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="StreamCategory">The category to construct from</param>
		public GetProjectCategoryResponse(StreamCategory StreamCategory)
		{
			this.Name = StreamCategory.Name;
			this.Row = StreamCategory.Row;
			this.ShowOnNavMenu = StreamCategory.ShowOnNavMenu;
			this.IncludePatterns = StreamCategory.IncludePatterns;
			this.ExcludePatterns = StreamCategory.ExcludePatterns;
		}
	}

	/// <summary>
	/// Response describing a project
	/// </summary>
	public class GetProjectResponse
	{
		/// <summary>
		/// Unique id of the project
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Name of the project
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// Order to display this project on the dashboard
		/// </summary>
		public int Order { get; set; }

		/// <summary>
		/// List of streams that are in this project
		/// </summary>
		public List<GetProjectStreamResponse>? Streams { get; set; }

		/// <summary>
		/// List of stream categories to display
		/// </summary>
		public List<GetProjectCategoryResponse>? Categories { get; set; }

		/// <summary>
		/// Properties for this project.
		/// </summary>
		public Dictionary<string, string> Properties { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Parameterless constructor for serialization
		/// </summary>
		private GetProjectResponse()
		{
			Id = null!;
			Name = null!;
			Properties = null!;
			Streams = null!;
			Categories = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id of the project</param>
		/// <param name="Name">Name of the project</param>
		/// <param name="Order">Order to show this project on the dashboard</param>
		/// <param name="Streams">List of streams to display</param>
		/// <param name="Categories">List of stream categories to display</param>
		/// <param name="Properties">Properties for this project</param>
		/// <param name="Acl">Custom permissions for this object</param>
		public GetProjectResponse(string Id, string Name, int Order, List<GetProjectStreamResponse>? Streams, List<GetProjectCategoryResponse>? Categories, Dictionary<string, string> Properties, GetAclResponse? Acl)
		{
			this.Id = Id;
			this.Name = Name;
			this.Order = Order;
			this.Streams = Streams;
			this.Categories = Categories;
			this.Properties = Properties;
			this.Acl = Acl;
		}
	}
}
