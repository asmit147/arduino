#!/bin/sh
cd "$(dirname $0)"
arduino --board arduino:avr:leonardo --port /dev/ttyACM0 --upload pump-control.c --verbose
