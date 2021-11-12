import moment from "moment";
import { GetIssueResponse, StreamData } from "../../backend/Api";
import dashboard from "../../backend/Dashboard";
import { projectStore } from "../../backend/ProjectStore";
import { displayTimeZone } from "./timeUtils";

export type IssueJira = {
	description: string;
	link: string;
}

// This regex could be optimized, though it works
// eslint-disable-next-line
const urlRegex = /([\w+]+\:\/\/)?([\w\d-]+\.)*[\w-]+[\.\:]\w+([\/\?\=\&\#\.]?[\w-]+)*\/?/gm

export function getIssueJiras(issue: GetIssueResponse): IssueJira[] {

	if (!issue.description) {
		return [];
	}

	const jiras: IssueJira[] = [];

	const matches = Array.from(issue.description.matchAll(urlRegex));

	matches.forEach(m => {
		m.forEach(match => {

			if (!match) {
				return;
			}
			// @todo: add jira match to server config
			if (match.toLowerCase().startsWith("https://jira.it.epicgames.com")) {
				const path = new URL(match).pathname.split("/");
				if (path.length) {
					jiras.push({link: match, description: path[path.length - 1]})
				}
				
			}
		})
	})
	
	return jiras;

}

export function getIssueStatus(issue: GetIssueResponse, showResolveTime?: boolean): string {

	let text = "";

	if (issue.resolvedAt) {

		let resolvedText = "Resolved";

		if (issue.fixChange) {
			resolvedText += ` in CL ${issue.fixChange}`;
		}

		resolvedText += ` by ${issue.resolvedBy ?? "Horde"}`;

		if (!showResolveTime) {
			return resolvedText;
		}

		const displayTime = moment(issue.resolvedAt).tz(displayTimeZone());

		const format = dashboard.display24HourClock ? "MMM Do, HH:mm z" : "MMM Do, h:mma z";

		resolvedText += ` on ${displayTime.format(format)}`;

		return resolvedText;
	}

	if (!issue.ownerId) {
		text = "Currently unassigned.";
	} else {
		if (issue.ownerId === dashboard.userId) {
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

