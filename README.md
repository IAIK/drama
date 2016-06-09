# DRAMA Reverse-Engineering Tool and Side-Channel Tools
This repository contains several tools to reverse engineer the undocument DRAM addressing functions on Intel CPUs. These DRAM addressing functions uncovered a new side channel, enabling DRAMA (DRAM addressing) attacks.
These attacks exploit the DRAM row buffer that is shared, even in multi-processor systems. Apart from that our attack improves Rowhammer attacks and enabled the first successful Rowhammer attacks on DDR4 memory.

The "[DRAMA](https://www.usenix.org/conference/usenixsecurity16/technical-sessions/presentation/pessl)" paper by Pessl, Gruss, Maurice, Schwarz, and Mangard will be published at the Usenix Security Symposium 2016.

## One note before starting

**Warning:** This code is provided as-is. You are responsible for protecting yourself, your property and data, and others from any risks caused by this code. This code may not detect vulnerabilities of your applications. This code is only for testing purposes. Use it only on test systems which contain no sensitive data.

The programs should work on x86-64 Intel CPUs with a recent Linux. Note that the code contains hardcoded addresses and thresholds that are specific to our test systems. Please adapt these addresses and thresholds to your system.

## Reverse-engineering your DRAM addressing functions
TODO

## DRAMA side channel attack
Start by generating a histogram of row hits and row conflicts.
You can find a tool for that in the ''sc'' folder. You need to adapt the DRAM functions that are hard-coded in the source code right now to the functions you obtained in the reverse-engineering step.

Modify the spy tool in the ''sc'' folder to use the threshold you found by running the histogram tool.

The spy tool runs in an endless loop and will measure row hits on its own memory. As soon as it found a significant number of row hits it will switch to a short 30 second exploitation phase (as a proof-of-concept).
For instance, if you hold down a key in your webbrowser you will eventually find a DRAM row that has a significant number of row hits. This can be a matter of seconds to minutes. If you don't find anything, try again later, you likely will have other physical addresses then. As soon as the exploitation phase runs, you can check whether row hits on this row really are a reliable side channel for the event you want to spy on. If not, just let the search continue.

To speed up the search slightly and also to provide more information to the user, the spy tool uses ''/proc/self/pagemap''. Make sure the tool can access this file.

