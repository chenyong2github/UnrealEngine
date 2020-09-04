// Copyright Epic Games, Inc. All Rights Reserved.

import { AppInterface, Handler, WebRequest } from './webserver'
import { setDefault } from './helper'

const posted = new Map<string, number[]>()


export class DummySlackApp implements AppInterface {

	constructor(private req: WebRequest) {
	}

	@Handler('POST', '/api/*')
	post(command: string) {
		// @todo (maybe) construct app with logger
		console.log('POST', command, this.req.reqData)

		const data = this.req.reqData
		if (!data) {
			throw new Error('Nothing to post!')
		}
		const clMatch = data.match(/changes\/(\d+)\|\d+/)
		if (clMatch) {
			const channel = JSON.parse(data).channel
			console.log(JSON.parse(data).channel, clMatch[1])
			setDefault(posted, channel, []).push(parseInt(clMatch[1]))
		}

		// look for color for block or resolve?

		// careful, two changes for resolutions, luckily first is what we want
	}

	@Handler('GET', '/posted/*')
	channelMessages(channel: string) {
		console.log('GET POSTED!!!', channel, posted.size, posted.get(channel))
		return posted.get(channel) || []
	}
}
