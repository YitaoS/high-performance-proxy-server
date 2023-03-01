#!/bin/bash

make

chmod ug+w ./proxy

while true
do 
	./proxy 0.0.0.0 12345 50
	sleep 1 
done