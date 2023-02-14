#!/bin/bash

sudo perf record -F 99 -g -- $*
sudo ./gen-svg.sh