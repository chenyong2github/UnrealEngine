# Copyright Epic Games, Inc. All Rights Reserved.
#!/bin/sh

export FASTBUILD_BROKERAGE_PATH=/Volumes/FASTBuildBrokerage
`dirname "$0"`/Mac/FBuildWorker -mode=idle -cpus=-1
