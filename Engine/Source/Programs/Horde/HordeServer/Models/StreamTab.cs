// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Api;
using HordeServer.Utilities;
using MongoDB.Bson.Serialization.Attributes;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace HordeServer.Models
{
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Information about a page to display in the dashboard for a stream
	/// </summary>
	[BsonKnownTypes(typeof(JobsTab))]
	public abstract class StreamTab
	{
		/// <summary>
		/// Title of this page
		/// </summary>
		[BsonRequired]
		public string Title { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		private StreamTab()
		{
			Title = null!;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Title">Title of this page</param>
		public StreamTab(string Title)
		{
			this.Title = Title;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">Public request object</param>
		public StreamTab(CreateStreamTabRequest Request)
		{
			Title = Request.Title;
		}

		/// <summary>
		/// Creates an instance from a request
		/// </summary>
		/// <param name="Request">The request object</param>
		/// <returns>New tab instance</returns>
		public static StreamTab FromRequest(CreateStreamTabRequest Request)
		{
			CreateJobsTabRequest? JobsRequest = Request as CreateJobsTabRequest;
			if (JobsRequest != null)
			{
				return new JobsTab(JobsRequest);
			}

			throw new Exception($"Unknown tab request type '{Request.GetType()}'");
		}

		/// <summary>
		/// Creates a response object
		/// </summary>
		/// <returns>Response object</returns>
		public abstract GetStreamTabResponse ToResponse();
	}

	/// <summary>
	/// Describes a column to display on the jobs page
	/// </summary>
	[BsonKnownTypes(typeof(JobsTabLabelColumn), typeof(JobsTabParameterColumn))]
	public abstract class JobsTabColumn
	{
		/// <summary>
		/// Heading for this column
		/// </summary>
		public string Heading { get; set; }

		/// <summary>
		/// Relative width of this column.
		/// </summary>
		public int? RelativeWidth { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		protected JobsTabColumn()
		{
			Heading = null!;
			RelativeWidth = 1;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Heading">Heading for this column</param>
		/// <param name="RelativeWidth">Relative width of this column.</param>
		public JobsTabColumn(string Heading, int? RelativeWidth)
		{
			this.Heading = Heading;
			this.RelativeWidth = RelativeWidth;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">Public request object</param>
		public JobsTabColumn(CreateJobsTabColumnRequest Request)
		{
			this.Heading = Request.Heading;
			this.RelativeWidth = Request.RelativeWidth;
		}

		/// <summary>
		/// Converts this object to a JSON response object
		/// </summary>
		/// <returns>Response object</returns>
		public abstract GetJobsTabColumnResponse ToResponse();
	}

	/// <summary>
	/// Column that contains a set of labels
	/// </summary>
	public class JobsTabLabelColumn : JobsTabColumn
	{
		/// <summary>
		/// Category of labels to display in this column. If null, includes any label not matched by another column.
		/// </summary>
		public string? Category { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		protected JobsTabLabelColumn()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Heading">Heading for this column</param>
		/// <param name="Category">Category of labels to display in this column</param>
		/// <param name="RelativeWidth">Relative width of this column.</param>
		public JobsTabLabelColumn(string Heading, string? Category, int? RelativeWidth)
			: base(Heading, RelativeWidth)
		{
			this.Category = Category;
		}

		/// <summary>
		/// Converts this object to a JSON response object
		/// </summary>
		/// <returns>Response object</returns>
		public override GetJobsTabColumnResponse ToResponse()
		{
			return new GetJobsTabLabelColumnResponse(Heading, Category, RelativeWidth);
		}
	}

	/// <summary>
	/// Include the value for a parameter on the jobs tab
	/// </summary>
	public class JobsTabParameterColumn : JobsTabColumn
	{
		/// <summary>
		/// Name of a parameter to show in this column. Should be in the form of a prefix, eg. "-set:Foo="
		/// </summary>
		public string? Parameter { get; set; }

		/// <summary>
		/// Private constructor for serialization
		/// </summary>
		protected JobsTabParameterColumn()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Heading">Heading for this column</param>
		/// <param name="Parameter">Category of labels to display in this column</param>
		/// <param name="RelativeWidth">Relative width of this column.</param>
		public JobsTabParameterColumn(string Heading, string? Parameter, int? RelativeWidth)
			: base(Heading, RelativeWidth)
		{
			this.Parameter = Parameter;
		}

		/// <summary>
		/// Converts this object to a JSON response object
		/// </summary>
		/// <returns>Response object</returns>
		public override GetJobsTabColumnResponse ToResponse()
		{
			return new GetJobsTabParameterColumnResponse(Heading, Parameter, RelativeWidth);
		}
	}

	/// <summary>
	/// Describes a job page
	/// </summary>
	[BsonDiscriminator("Jobs")]
	public class JobsTab : StreamTab
	{
		/// <summary>
		/// Whether to show job names
		/// </summary>
		public bool ShowNames { get; set; }

		/// <summary>
		/// Names of jobs to include on this page. If there is only one name specified, the name column does not need to be displayed.
		/// </summary>
		public List<string>? JobNames { get; set; }

		/// <summary>
		/// List of templates to display on this tab.
		/// </summary>
		public List<TemplateRefId>? Templates { get; set; }

		/// <summary>
		/// Columns to display in the table
		/// </summary>
		public List<JobsTabColumn>? Columns { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Title">Title for this page</param>
		/// <param name="ShowNames">Show names of jobs on the page</param>
		/// <param name="Templates">Template names to include on this page</param>
		/// <param name="JobNames">Names of jobs to include on this page</param>
		/// <param name="Columns">Columns ot display in the table</param>
		public JobsTab(string Title, bool ShowNames, List<TemplateRefId>? Templates, List<string>? JobNames, List<JobsTabColumn>? Columns)
			: base(Title)
		{
			this.ShowNames = ShowNames;
			this.Templates = Templates;
			this.JobNames = JobNames;
			this.Columns = Columns;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Request">Public request object</param>
		public JobsTab(CreateJobsTabRequest Request)
			: base(Request)
		{
			ShowNames = Request.ShowNames;
			Templates = Request.Templates?.ConvertAll(x => new TemplateRefId(x));
			JobNames = Request.JobNames;
			Columns = Request.Columns?.ConvertAll(x => x.ToModel());
		}

		/// <inheritdoc/>
		public override GetStreamTabResponse ToResponse()
		{
			return new GetJobsTabResponse(Title, ShowNames, Templates?.ConvertAll(x => x.ToString()), JobNames, Columns?.ConvertAll(x => x.ToResponse()));
		}
	}
}
