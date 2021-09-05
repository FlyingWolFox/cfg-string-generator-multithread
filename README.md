# cfg-string-generator-multithread

A failed experiment to parallelize [cfg-string-generator](https://github.com/FlyingWolFox/cfg-string-generator), since it didn't bring speed improvements (in half of the cases it's slower).
This implements three algorithms: dual containers, which doesn't need heavy thread synchronization, but it's memory intensive, controlled queue, which uses blocking queues to reduce memory usage, and free queue, which uses information on the derivations to know when to stop. Curiously free queue is slower than controlled queue, using more or less the same memory

## Tests

The test results are present on the tests directory and show no speed gain on the dual container algorithm and speed loss in both queue algorithms. Memory usage was equivalent or higher. The "test suit" is in tests/. It's not optimized

## Possible reason

The most probably reason for the results is mutex contention with dequeuing and queueing. This is attested by thread CPU usage below 50% and process-wise CPU usage on a average 230% (in a 4 core, 8 thread CPU - Intel i5 8250U). The dual container algorithm probably loses time combining the outputs from the threads and multiple allocations

## It's fixable?

Probably. Making the thread use bulk queuing and dequeuing should speed things up. In the case on dual container maybe use less moving/copying would improve performance, using more memory probably, but maybe the queues can be faster than a good version of this (it's teh case on the single threaded version).

## Contributing

If you want to try fix this yourself, the speed/memory test suit is ready. This repo contains both single and multi threaded code, but the first isn't synced with its repo and is, possibly, outdated. The code here is unpolished (actually I didn't test if the the multi threaded code generates correctly, but the output right). If you made a better version of this, feel free to make a pull request
