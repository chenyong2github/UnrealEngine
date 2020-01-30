// Copyright Epic Games, Inc. All Rights Reserved.

export interface Arg<T> {
	match: RegExp,
	parse?: (str: string) => T,
	env: string,
	dflt?: T
}

export function readProcessArgs(supportedArgs: {[param: string]: (Arg<any>)}) {
	const args: any = {}
	for (let argName in supportedArgs) {
		let computedValue = undefined
		let rec = supportedArgs[argName]
		if (rec.env) {
			let envVal = process.env[rec.env]
			if (envVal) {
				computedValue = rec.parse ? rec.parse(envVal) : envVal
			}
		}

		for (const val of process.argv.slice(2)) {
			let match = val.match(rec.match)
			if (match) {
				computedValue = rec.parse ? rec.parse(match[1]) : match[1]
			}
		}

		if (computedValue === undefined) {
			if (rec.dflt !== undefined) {
				computedValue = rec.dflt
			}
			else {
				if (rec.env) {
					console.log(`Missing required ${rec.env} environment variable or -${argName}=<foo> parameter.`)
				}
				else {
					console.log(`Missing required -${argName}=<foo> parameter.`)
				}
				return null
			}
		}
		args[argName] = computedValue
	}
	return args
}