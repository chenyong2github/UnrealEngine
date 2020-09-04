// Copyright Epic Games, Inc. All Rights Reserved.

import * as util from 'util';
import * as request from './request';

// from UnrealGameSync/PostBadgeStatus
interface BuildData {
	BuildType: string,
	Url: string,
	Project: string,
	ArchivePath: string,
	ChangeNumber: number,
	Result: string // starting, failure, warning, success, or skipped
}

export class Badge {
	static externalRobomergeUrl = process.env["ROBO_EXTERNAL_URL"] || "https://robomerge";

	static markStarting(badge: string, project: string, cl: number, bot: string) { this.mark(Badge.STARTING, badge, project, cl, bot) }
	static markSuccess(badge: string, project: string, cl: number, bot: string) { this.mark(Badge.SUCCESS, badge, project, cl, bot) }
	static markFailure(badge: string, project: string, cl: number, bot: string) { this.mark(Badge.FAILURE, badge, project, cl, bot) }
	static markWarning(badge: string, project: string, cl: number, bot: string) { this.mark(Badge.WARNING, badge, project, cl, bot) }
	static markSkipped(badge: string, project: string, cl: number, bot: string) { this.mark(Badge.SKIPPED, badge, project, cl, bot) }

	static STARTING = 'Starting'
	static SUCCESS = 'Success'
	static FAILURE = 'Failure'
	static WARNING = 'Warning'
	static SKIPPED = 'Skipped'

	static mark(result: string, badge: string, project: string, cl: number, bot: string) {
		const data: BuildData = {
			BuildType: badge,
			Url: this.externalRobomergeUrl + '#' + bot,
			Project: project,
			ArchivePath: '',
			ChangeNumber: cl,
			Result: result
		}

		// Promise.resolve('')
		request.post({
			url: 'http://ugsapi.epicgames.net/api/CIS',
			body: JSON.stringify(data),
			contentType: 'application/json'
		})
		.then((error: string) => util.log(error ? 'Post got success code but error string: ' + error :
			`Added '${badge}' (${result}) UGS badge to ${project}@${cl}`
		), (error) => util.log('Badge error: ' + error.toString()))
	}

	static test() {
		this.markSuccess('iOS', '//GamePlugins/Main/PluginTestGame', 4107341, 'OOBLE')
	}
}

if (process.argv.indexOf('__TEST__') !== -1) {
	Badge.test()
}