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
		/// Constructor
		/// </summary>
		public CreateProjectCategoryRequest(string Name)
		{
			this.Name = Name;
		}
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
		/// Custom permissions for this object
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Id">Unique id of the project</param>
		/// <param name="Name">Name of the project</param>
		/// <param name="Order">Order to show this project on the dashboard</param>
		/// <param name="Streams">List of streams to display</param>
		/// <param name="Categories">List of stream categories to display</param>
		/// <param name="Acl">Custom permissions for this object</param>
		public GetProjectResponse(string Id, string Name, int Order, List<GetProjectStreamResponse>? Streams, List<GetProjectCategoryResponse>? Categories, GetAclResponse? Acl)
		{
			this.Id = Id;
			this.Name = Name;
			this.Order = Order;
			this.Streams = Streams;
			this.Categories = Categories;
			this.Acl = Acl;
		}
	}
}
