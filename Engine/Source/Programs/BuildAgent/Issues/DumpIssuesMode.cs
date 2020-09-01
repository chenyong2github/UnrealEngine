// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using Tools.DotNETCommon;

namespace BuildAgent.Issues
{
	[ProgramMode("DumpIssues", "Updates the UGS build health system with output from completed builds")]
	class DumpIssuesMode : ProgramMode
	{
		class IssueResponse
		{
			public int Id { get; set; }
			public string Summary { get; set; }
			public string Owner { get; set; }
			public int FixChange { get; set; }
			public DateTime? CreatedAt { get; set; }
			public DateTime? ResolvedAt { get; set; }
			public DateTime? AcknowledgedAt { get; set; }
		}

		[CommandLine("-Server=", Required = true)]
		[Description("Url of the UGS metadata service")]
		string ServerUrl = null;

		[CommandLine("-Output=", Required = true)]
		[Description("Output CSV file to write")]
		string OutputFile = null;

		public override int Execute()
		{
			HttpWebRequest Request = (HttpWebRequest)WebRequest.Create(ServerUrl + "/api/issues?includeresolved=true");
			Request.ContentType = "application/json";
			Request.Method = "GET";

			List<IssueResponse> Issues;
			using (HttpWebResponse Response = (HttpWebResponse)Request.GetResponse())
			{
				using (StreamReader ResponseReader = new StreamReader(Response.GetResponseStream(), Encoding.Default))
				{
					string ResponseContent = ResponseReader.ReadToEnd();
					Issues = new JavaScriptSerializer() { MaxJsonLength = int.MaxValue }.Deserialize<List<IssueResponse>>(ResponseContent);
				}
			}

			FileReference OutputLocation = new FileReference(OutputFile);
			Log.TraceInformation("Writing {0}...", OutputLocation.FullName);

			using (StreamWriter Writer = new StreamWriter(OutputLocation.FullName))
			{
				Writer.WriteLine("Id,Summary,Owner,FixChange,CreatedAt,ResolvedAt,AcknowledgedAt");
				foreach(IssueResponse Issue in Issues)
				{
					Writer.WriteLine("{0},{1},{2},{3},{4},{5},{6}", Issue.Id, EscapeText(Issue.Summary), EscapeText(Issue.Owner), Issue.FixChange, Issue.CreatedAt, Issue.ResolvedAt, Issue.AcknowledgedAt);
				}
			}

			return 0;
		}

		static string EscapeText(string Text)
		{
			if(Text == null)
			{
				return Text;
			}

			Text = Text.Replace("\\", "\\\\");
			Text = Text.Replace(",", "\\,");
			Text = Text.Replace("'", "\\'");
			Text = Text.Replace("\"", "\\\"");
			return String.Format("\"{0}\"", Text);
		}
	}
}
