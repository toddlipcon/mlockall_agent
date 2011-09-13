#!/bin/bash

gcc -Wall -fPIC -shared -I$JAVA_HOME/include -I$JAVA_HOME/include/linux -o libmlockall_agent.so mlockall_agent.c
