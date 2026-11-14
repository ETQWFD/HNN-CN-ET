#!/bin/sh
set -e
cd "$(dirname "$0")"

export CGO_ENABLED=0
B="go build -trimpath -ldflags -s -w"

GOOS=linux GOARCH=amd64 $B -o ai-go-linux-amd64 .
GOOS=linux GOARCH=arm64 $B -o ai-go-linux-arm64 .
GOOS=linux GOARCH=arm GOARM=7 $B -o ai-go-linux-armv7 .
GOOS=windows GOARCH=amd64 $B -o ai-go.exe .

echo "done:"
ls -lh ai-go-linux-amd64 ai-go-linux-arm64 ai-go-linux-armv7 ai-go.exe
