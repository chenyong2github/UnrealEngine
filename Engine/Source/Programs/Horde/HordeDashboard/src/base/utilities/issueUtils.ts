import moment from "moment";
import { GetIssueResponse, StreamData } from "../../backend/Api";
import dashboard from "../../backend/Dashboard";
import { projectStore } from "../../backend/ProjectStore";
import { displayTimeZone } from "./timeUtils";


export function getIssueStatus(issue: GetIssueResponse, showResolveTime?:boolean): string {

	let text = "";	

	if (issue.resolvedAt) {

		if (!showResolveTime) {

			if (!issue.fixChange) {
				return `Resolved`;
			}
			return `Resolved in CL ${issue.fixChange}`;			
		}

		const displayTime = moment(issue.resolvedAt).tz(displayTimeZone());

		const format = dashboard.display24HourClock ? "MMM Do, HH:mm z" : "MMM Do, h:mma z";

		let text = `Resolved at ${displayTime.format(format)}`;

		if (issue.fixChange) {
			text += ` in CL ${issue.fixChange}`;
		}

		return text;

	}

	if (!issue.owner) {
		text = "Currently unassigned.";
	} else {
		if (issue.owner.toLowerCase() === dashboard.username?.toLowerCase()) {
			if (issue.nominatedBy) {
				text = `You have been nominated to fix this issue by ${issue.nominatedBy}.`;
			} else {
				text = `Assigned to ${issue.owner}.`;
			}
		} else {
			text = `Assigned to ${issue.owner}`;
			if (issue.nominatedBy) {
				text += ` by ${issue.nominatedBy}`;
			}
			if (!issue.acknowledgedAt) {
				text += ` (Unacknowledged)`;
			} else {
				text += ` (Acknowledged)`;
			}
			text += ".";
		}
	}

	return text;

}

export function generateStreamSummary(issue: GetIssueResponse): string {

	const streams: StreamData[] = [];

	projectStore.projects.forEach(p => {
		if (!p.streams) {
			return;
		}

		p.streams.filter(s => issue.unresolvedStreams.indexOf(s.id) !== -1).forEach(stream => {
			if (!streams.find(s => s.id === stream.id)) {
				streams.push(stream);
			}
		});
	});

	if (streams.length === 0) {
		return "";
	}

	const present = streams.slice(0, 3);
	const others = streams.slice(3);

	let text = "Affects " + present.map(s => `//${s.project?.name}/${s.name}`).join(", ");

	if (others.length) {
		text += ` and ${others.length} other streams.`;
	}

	return text;

}

